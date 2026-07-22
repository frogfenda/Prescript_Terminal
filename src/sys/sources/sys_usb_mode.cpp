/*
【模块职责】TinyUSB CDC/MSC 接口注册、BTN2模式决策以及MSC生命周期协调。
【边界】FAT文件系统与块读写细节全部委托给 HAL::FatStorage。
*/
#include "sys/sys_usb_mode.h"

#include "bsp/bsp_pins.h"
#include "hal/hal_fat_storage.h"
#include "sys/sys_sleep_scheduler.h"
#include "sys/sys_usb_cdc_serial.h"

#include <USB.h>
#include <esp32-hal-tinyusb.h>
#include <algorithm>
#include <cstring>

namespace
{
    USBCDC *s_cdc = &CDCSerial;
    SysUsbMode::Mode s_mode = SysUsbMode::Mode::CdcOnly;
    SysUsbMode::Error s_error = SysUsbMode::Error::None;
    bool s_started = false;
    bool s_mscActive = false;
    volatile bool s_ejectRequested = false;
    uint32_t s_pendingEvents = 0;
    uint32_t s_readOps = 0;
    uint32_t s_writeOps = 0;
    uint32_t s_readBytes = 0;
    uint32_t s_writeBytes = 0;
    uint32_t s_ioErrors = 0;
    uint32_t s_hostAccessObserved = 0;
    bool s_mscMediaPresent = false;
    HAL::FatStorage::Geometry s_mscGeometry;
    char s_mscVendorId[9] = "FOGFENDA";
    char s_mscProductId[17] = "ESP32S3 FAT";
    char s_mscRevision[5] = "1.0";

    enum EventBits : uint32_t
    {
        EVENT_USB_STARTED = 1U << 0,
        EVENT_USB_STOPPED = 1U << 1,
        EVENT_USB_SUSPENDED = 1U << 2,
        EVENT_USB_RESUMED = 1U << 3,
        EVENT_MEDIA_STARTED = 1U << 4,
        EVENT_MEDIA_STOPPED = 1U << 5,
        EVENT_MEDIA_EJECTED = 1U << 6,
        EVENT_CDC_OPENED = 1U << 7,
        EVENT_CDC_CLOSED = 1U << 8,
        EVENT_EJECT_IGNORED = 1U << 9,
    };

    void queueEvent(uint32_t event)
    {
        __atomic_fetch_or(&s_pendingEvents, event, __ATOMIC_RELAXED);
    }

    int32_t onMscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize)
    {
        const int32_t result = HAL::FatStorage::Read(lba, offset, buffer, bufferSize);
        if (result >= 0)
        {
            __atomic_store_n(&s_hostAccessObserved, 1U, __ATOMIC_RELAXED);
            __atomic_fetch_add(&s_readOps, 1U, __ATOMIC_RELAXED);
            __atomic_fetch_add(&s_readBytes, static_cast<uint32_t>(result), __ATOMIC_RELAXED);
        }
        else
        {
            __atomic_fetch_add(&s_ioErrors, 1U, __ATOMIC_RELAXED);
        }
        return result;
    }

    int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufferSize)
    {
        const int32_t result = HAL::FatStorage::Write(lba, offset, buffer, bufferSize);
        if (result >= 0)
        {
            __atomic_store_n(&s_hostAccessObserved, 1U, __ATOMIC_RELAXED);
            __atomic_fetch_add(&s_writeOps, 1U, __ATOMIC_RELAXED);
            __atomic_fetch_add(&s_writeBytes, static_cast<uint32_t>(result), __ATOMIC_RELAXED);
        }
        else
        {
            __atomic_fetch_add(&s_ioErrors, 1U, __ATOMIC_RELAXED);
        }
        return result;
    }

    bool onMscStartStop(uint8_t powerCondition, bool start, bool loadEject)
    {
        (void)powerCondition;
        if (!s_mscActive)
            return true;
        queueEvent(start ? EVENT_MEDIA_STARTED : EVENT_MEDIA_STOPPED);
        if (loadEject && !start)
        {
            // 枚举失败或介质尚未被实际读取时，Windows 也可能发送一次卸载命令。
            // 只有主机已成功访问过 FAT 块后，才把它认作用户发起的安全弹出。
            if (__atomic_load_n(&s_hostAccessObserved, __ATOMIC_RELAXED) == 0)
            {
                queueEvent(EVENT_EJECT_IGNORED);
                return true;
            }
            s_ejectRequested = true;
            queueEvent(EVENT_MEDIA_EJECTED);
        }
        return true;
    }

    void onUsbEvent(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
    {
        (void)arg;
        (void)eventData;
        if (eventBase != ARDUINO_USB_EVENTS)
            return;

        switch (eventId)
        {
        case ARDUINO_USB_STARTED_EVENT:
            // 当前 USB PHY/协议栈不能跨应用 Light Sleep 保持枚举状态。
            SysSleep_SetBlocker(SysSleepBlocker::UsbDevice, true);
            queueEvent(EVENT_USB_STARTED);
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            SysSleep_SetBlocker(SysSleepBlocker::UsbDevice, false);
            queueEvent(EVENT_USB_STOPPED);
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            queueEvent(EVENT_USB_SUSPENDED);
            break;
        case ARDUINO_USB_RESUME_EVENT:
            queueEvent(EVENT_USB_RESUMED);
            break;
        default:
            break;
        }
    }

    void onCdcEvent(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
    {
        (void)arg;
        if (eventBase != ARDUINO_USB_CDC_EVENTS || eventId != ARDUINO_USB_CDC_LINE_STATE_EVENT || !eventData)
            return;

        const auto *data = static_cast<const arduino_usb_cdc_event_data_t *>(eventData);
        // TinyUSB 以 DTR 表示串口已打开；RTS 是否置位由 VOFA+ 等上位机自行决定。
        queueEvent(data->line_state.dtr ? EVENT_CDC_OPENED : EVENT_CDC_CLOSED);
    }

    template <size_t N>
    void copyMscIdentity(char (&target)[N], const char *source, const char *fallback)
    {
        const char *value = source && source[0] ? source : fallback;
        std::memset(target, 0, N);
        std::strncpy(target, value, N - 1);
    }

    void rollbackMsc()
    {
        s_mscMediaPresent = false;
        s_mscActive = false;
        s_mscGeometry = HAL::FatStorage::Geometry{};
        HAL::FatStorage::CloseForUsb();
    }
}

/*
 * CDC 始终由 Arduino core 注册为接口 0/1；MSC 使用 CUSTOM 槽位追加到接口 2。
 * 两种启动模式拥有完全相同的 CDC 接口和 PID，Windows 不会再为 MSC 模式创建第二个 COM。
 */
extern "C" uint16_t fogfendaMscLoadDescriptor(uint8_t *destination, uint8_t *interfaceNumber)
{
    if (!destination || !interfaceNumber)
        return 0;

    const uint8_t endpoint = tinyusb_get_free_duplex_endpoint();
    if (endpoint == 0)
        return 0;

    const uint8_t stringIndex = tinyusb_add_string_descriptor("Fogfenda FAT MSC");
    const uint8_t descriptor[TUD_MSC_DESC_LEN] = {
        TUD_MSC_DESCRIPTOR(*interfaceNumber, stringIndex, endpoint,
                           static_cast<uint8_t>(0x80U | endpoint), 64)};
    *interfaceNumber += 1;
    std::memcpy(destination, descriptor, sizeof(descriptor));
    return sizeof(descriptor);
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void)
{
    return 0;
}

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendorId[8],
                                    uint8_t productId[16], uint8_t productRevision[4])
{
    (void)lun;
    std::memset(vendorId, ' ', 8);
    std::memset(productId, ' ', 16);
    std::memset(productRevision, ' ', 4);
    std::memcpy(vendorId, s_mscVendorId, std::min<size_t>(std::strlen(s_mscVendorId), 8));
    std::memcpy(productId, s_mscProductId, std::min<size_t>(std::strlen(s_mscProductId), 16));
    std::memcpy(productRevision, s_mscRevision, std::min<size_t>(std::strlen(s_mscRevision), 4));
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    if (!s_mscMediaPresent)
    {
        tud_msc_set_sense(0, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *blockCount, uint16_t *blockSize)
{
    (void)lun;
    if (!blockCount || !blockSize)
        return;
    *blockCount = s_mscMediaPresent ? s_mscGeometry.blockCount : 0;
    *blockSize = s_mscMediaPresent ? s_mscGeometry.blockSize : 0;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t powerCondition, bool start, bool loadEject)
{
    (void)lun;
    return onMscStartStop(powerCondition, start, loadEject);
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                     void *buffer, uint32_t bufferSize)
{
    (void)lun;
    return s_mscMediaPresent ? onMscRead(lba, offset, buffer, bufferSize) : -1;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                      uint8_t *buffer, uint32_t bufferSize)
{
    (void)lun;
    return s_mscMediaPresent ? onMscWrite(lba, offset, buffer, bufferSize) : -1;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsiCommand[16],
                                   void *buffer, uint16_t bufferSize)
{
    (void)buffer;
    (void)bufferSize;
    if (!s_mscMediaPresent || !scsiCommand)
        return -1;
    if (scsiCommand[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL)
        return 0;

    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

namespace SysUsbMode
{
    Mode DecideFromBtn2(uint32_t settleMs, uint8_t samples, uint32_t sampleGapMs)
    {
        pinMode(BSP::Pins::BTN_SIDE, INPUT_PULLUP);
        if (settleMs > 0)
            delay(settleMs);

        if (samples == 0)
            samples = 1;

        uint8_t pressedSamples = 0;
        for (uint8_t i = 0; i < samples; ++i)
        {
            if (digitalRead(BSP::Pins::BTN_SIDE) == LOW)
                ++pressedSamples;
            if (sampleGapMs > 0 && i + 1 < samples)
                delay(sampleGapMs);
        }

        return pressedSamples == samples ? Mode::CdcWithMsc : Mode::CdcOnly;
    }

    bool Begin(Mode mode, const Config &config)
    {
        s_error = Error::None;

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
        (void)mode;
        (void)config;
        s_error = Error::HardwareUsbModeEnabled;
        return false;
#elif defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
        (void)mode;
        (void)config;
        s_error = Error::AutomaticUsbStartupEnabled;
        return false;
#else
        if (s_started)
        {
            if (s_mode == mode)
                return true;
            s_error = Error::AlreadyStartedWithDifferentMode;
            return false;
        }

        if (mode == Mode::CdcWithMsc && !HAL::FatStorage::OpenForUsb(config.fatPartitionLabel))
        {
            s_error = Error::FatBackendUnavailable;
            return false;
        }

        if (!CDCSerial.Configure(config.cdcRxBufferSize, config.cdcTxTimeoutMs))
        {
            HAL::FatStorage::CloseForUsb();
            s_error = Error::AllocationFailed;
            return false;
        }

        s_cdc->begin(config.cdcBaud);
        // 正常 CDC-only 启动使用 Arduino-ESP32 原生 1200bps/DTR 下载流程；
        // MSC 模式禁止串口触发下载，避免电脑打开 CDC 时中断 FAT 会话。
        s_cdc->enableReboot(mode == Mode::CdcOnly);
        s_cdc->onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onCdcEvent);

        __atomic_store_n(&s_pendingEvents, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_readOps, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_writeOps, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_readBytes, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_writeBytes, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_ioErrors, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_hostAccessObserved, 0U, __ATOMIC_RELAXED);

        if (mode == Mode::CdcWithMsc)
        {
            s_mscGeometry = HAL::FatStorage::GetGeometry();
            copyMscIdentity(s_mscVendorId, config.mscVendorId, "FOGFENDA");
            copyMscIdentity(s_mscProductId, config.mscProductId, "ESP32S3 FAT");
            copyMscIdentity(s_mscRevision, config.mscRevision, "1.0");

            if (s_mscGeometry.blockCount == 0 || s_mscGeometry.blockSize == 0 ||
                tinyusb_enable_interface(USB_INTERFACE_CUSTOM, TUD_MSC_DESC_LEN,
                                         fogfendaMscLoadDescriptor) != ESP_OK)
            {
                rollbackMsc();
                s_error = Error::MscConfigurationFailed;
                return false;
            }

            // 介质在 USB 枚举前就绪；不在枚举后动态挂载，避免 Windows 进入约 10 秒的磁盘重试退避。
            s_mscMediaPresent = true;
            s_mscActive = true;
        }
        else
        {
            s_mscGeometry = HAL::FatStorage::Geometry{};
            s_mscMediaPresent = false;
            s_mscActive = false;
        }

        USB.VID(config.usbVid);
        USB.PID(config.usbPid);
        USB.firmwareVersion(config.usbFirmwareVersion);
        if (config.manufacturer)
            USB.manufacturerName(config.manufacturer);
        if (config.productName)
            USB.productName(config.productName);
        if (config.serialNumber)
            USB.serialNumber(config.serialNumber);

        USB.onEvent(onUsbEvent);
        // 在 USB.begin() 之前就锁定模式，避免主机极快发送 1200bps 时误走 CDC-only 分支。
        s_mode = mode;
        if (!USB.begin())
        {
            rollbackMsc();
            s_cdc->end();
            s_mode = Mode::CdcOnly;
            s_error = Error::UsbStartFailed;
            return false;
        }

        s_started = true;
        s_ejectRequested = false;
        return true;
#endif
    }

    bool BeginFromBtn2(const Config &config)
    {
        return Begin(DecideFromBtn2(), config);
    }

    void StopMsc()
    {
        Service();
        rollbackMsc();
        if (s_mode == Mode::CdcWithMsc)
            s_mode = Mode::CdcOnly;
        Serial.println("[FATFS][MSC] 原始块后端已关闭，PC 的 FAT 访问权已释放。");
    }

    void Service()
    {
        CDCSerial.Service();

        (void)__atomic_exchange_n(&s_pendingEvents, 0U, __ATOMIC_ACQ_REL);
    }

    void DisconnectBeforeRestart(uint32_t settleMs)
    {
        if (!s_started)
            return;

        Serial.println("[USB] 正在主动断开 USB，完成后执行软件重启。");
        CDCSerial.Service();
        delay(50);

        s_mscMediaPresent = false;
        tud_disconnect();

        // 先等待 TinyUSB 确认主机已撤销配置，再给 Windows 的复合设备子节点留下完整清理窗口。
        const uint32_t unmountStartedAt = millis();
        while (tud_mounted() && millis() - unmountStartedAt < 1000)
            delay(10);

        const uint32_t startedAt = millis();
        while (millis() - startedAt < settleMs)
            delay(10);
    }

    bool IsStarted()
    {
        return s_started;
    }

    bool IsMscActive()
    {
        return s_mscActive;
    }

    Mode CurrentMode()
    {
        return s_mode;
    }

    Error LastError()
    {
        return s_error;
    }

    const char *LastErrorText()
    {
        switch (s_error)
        {
        case Error::None:
            return "none";
        case Error::HardwareUsbModeEnabled:
            return "ARDUINO_USB_MODE must be 0 for TinyUSB";
        case Error::AutomaticUsbStartupEnabled:
            return "ARDUINO_USB_CDC_ON_BOOT must be 0 for conditional MSC";
        case Error::AlreadyStartedWithDifferentMode:
            return "USB already started with another mode";
        case Error::FatBackendUnavailable:
            return "FAT wear-levelling backend unavailable";
        case Error::AllocationFailed:
            return "USB object allocation failed";
        case Error::MscConfigurationFailed:
            return "USB MSC configuration failed";
        case Error::UsbStartFailed:
            return "TinyUSB start failed";
        default:
            return "unknown";
        }
    }

    USBCDC *Cdc()
    {
        return s_started ? s_cdc : nullptr;
    }

    bool ConsumeEjectRequest()
    {
        const bool requested = s_ejectRequested;
        s_ejectRequested = false;
        return requested;
    }
}
