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
#include <esp_attr.h>
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
    uint32_t s_bootloaderRequested = 0;
    uint32_t s_bootloaderAllowed = 0;
    bool s_mscMediaPresent = false;
    HAL::FatStorage::Geometry s_mscGeometry;
    char s_mscVendorId[9] = "FOGFENDA";
    char s_mscProductId[17] = "ESP32S3 FAT";
    char s_mscRevision[5] = "1.0";
    /*
     * Windows 对本设备存在实测怪癖：GET_MAX_LUN 必须返回 1（即宣称 LUN 0/1 两个逻辑单元），
     * 否则 Windows 要花很长时间才把 U 盘识别出来。这是提交 1ae3a00“修复u盘识别时间较长问题”
     * 验证过的行为，不能按“单卷应返回 0”的协议常理改回去。
     * LUN 1 与 LUN 0 指向同一个 FAT 卷，等价于旧固件忽略 lun 参数直接服务同一后端。
     */
    constexpr uint8_t MSC_MAX_LUN = 1;

    /**
     * 【函数说明】确认主机请求的 LUN 在本设备声明范围内（0～MSC_MAX_LUN）。
     * 【调用约束】配合 tud_msc_get_maxlun_cb 返回 MSC_MAX_LUN 使用；LUN 1 是 Windows 快速识别
     * 所需的别名，与 LUN 0 共用同一个 FAT 后端，不能再按“单卷只允许 LUN 0”拒绝。
     */
    constexpr bool isSupportedMscLun(uint8_t lun)
    {
        return lun <= MSC_MAX_LUN;
    }

    /**
     * 【函数说明】为越界LUN写入SCSI“逻辑单元不受支持”状态，防止未来描述符或主机异常请求
     * 被静默映射到同一个FAT wear-levelling后端。
     */
    void setUnsupportedLunSense(uint8_t lun)
    {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x25, 0x00);
    }

    /*
     * 1200bps上传请求不能在完整应用已经加载大量PSRAM资源后直接切换USB PHY。
     * RTC_NOINIT内存可跨普通软件重启保留，因此先记录一次性中继请求，再让下一次
     * 最小启动在业务模块初始化前调用Arduino Core官方的ROM下载器切换入口。
     * 三字段同时校验，避免上电时未初始化的RTC内存被误认成有效上传请求。
     */
    struct BootloaderRelayGuard
    {
        uint32_t magic;
        uint32_t command;
        uint32_t check;
    };

    constexpr uint32_t BOOTLOADER_RELAY_MAGIC = 0x55534252UL; // ASCII "USBR"。
    constexpr uint32_t BOOTLOADER_RELAY_COMMAND = 0x00000001UL;
    constexpr uint32_t BOOTLOADER_RELAY_XOR = 0xA55A3CC3UL;
    RTC_NOINIT_ATTR BootloaderRelayGuard s_bootloaderRelay;

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

    constexpr uint32_t USB_UNMOUNT_TIMEOUT_MS = 1000;

    bool hasBootloaderRelayRequest()
    {
        return s_bootloaderRelay.magic == BOOTLOADER_RELAY_MAGIC &&
               s_bootloaderRelay.command == BOOTLOADER_RELAY_COMMAND &&
               s_bootloaderRelay.check ==
                   (BOOTLOADER_RELAY_COMMAND ^ BOOTLOADER_RELAY_XOR);
    }

    void storeBootloaderRelayRequest()
    {
        // magic最后写入，防止复位恰好发生在字段更新中途时留下半有效请求。
        s_bootloaderRelay.magic = 0;
        s_bootloaderRelay.command = BOOTLOADER_RELAY_COMMAND;
        s_bootloaderRelay.check = BOOTLOADER_RELAY_COMMAND ^ BOOTLOADER_RELAY_XOR;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        s_bootloaderRelay.magic = BOOTLOADER_RELAY_MAGIC;
    }

    void clearBootloaderRelayRequest()
    {
        // 先清magic保证官方入口即使异常返回或再次复位，也不会形成重启循环。
        s_bootloaderRelay.magic = 0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        s_bootloaderRelay.command = 0;
        s_bootloaderRelay.check = 0;
    }

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
        if (eventBase != ARDUINO_USB_CDC_EVENTS || !eventData)
            return;

        const auto *data = static_cast<const arduino_usb_cdc_event_data_t *>(eventData);
        if (eventId == ARDUINO_USB_CDC_LINE_CODING_EVENT)
        {
            /*
             * Arduino Core的enableReboot(true)会在TinyUSB设备任务的回调栈内同步切换PHY。
             * 本项目还要统一执行MSC写入保护和主循环状态门控，因此回调这里只提交请求；
             * 主循环随后仍调用Core的完整下载入口，不自行复制底层PHY或复位寄存器操作。
             */
            if (data->line_coding.bit_rate == 1200 &&
                __atomic_load_n(&s_bootloaderAllowed, __ATOMIC_ACQUIRE) != 0)
            {
                __atomic_store_n(&s_bootloaderRequested, 1U, __ATOMIC_RELEASE);
            }
            return;
        }

        if (eventId != ARDUINO_USB_CDC_LINE_STATE_EVENT)
            return;

        // TinyUSB 以 DTR 表示串口已打开；RTS 是否置位由 VOFA+ 等上位机自行决定。
        queueEvent(data->line_state.dtr ? EVENT_CDC_OPENED : EVENT_CDC_CLOSED);

        /*
         * 如果主机曾在 MSC 保护期把串口速率设为 1200，USBCDC 后续再次收到相同速率时
         * 不会重复产生 LINE_CODING 事件。PlatformIO 的 1200bps touch 一定会打开再关闭
         * 端口，因此在 DTR 撤销时再检查一次当前速率，保证安全弹出后的下一次上传仍能触发。
         */
        if (!data->line_state.dtr && s_cdc->baudRate() == 1200 &&
            __atomic_load_n(&s_bootloaderAllowed, __ATOMIC_ACQUIRE) != 0)
        {
            __atomic_store_n(&s_bootloaderRequested, 1U, __ATOMIC_RELEASE);
        }
    }

    // 【函数说明】普通软件重启前主动撤销 TinyUSB 上拉并等待主机确认卸载；只能从 Arduino 主循环调用。
    void disconnectTinyUsb(uint32_t settleMs)
    {
        s_mscMediaPresent = false;
        tud_disconnect();

        const uint32_t unmountStartedAt = millis();
        while (tud_mounted() && millis() - unmountStartedAt < USB_UNMOUNT_TIMEOUT_MS)
            delay(10);

        const uint32_t settleStartedAt = millis();
        while (millis() - settleStartedAt < settleMs)
            delay(10);
    }

    /*
     * 【函数说明】消费一次性中继请求，并通过Arduino Core官方入口切换到ROM下载器。
     * 【调用约束】只允许在USB.begin()成功后、任何业务模块初始化前调用。
     * 【关键原因】ESP32-S3的同一组USB引脚由TinyUSB OTG与ROM USB-Serial/JTAG复用；
     * usb_persist_restart()负责完整的PHY交接、BUS_RESET握手和ROM下载标志写入。
     */
    void enterPendingRomBootloader()
    {
        if (!hasBootloaderRelayRequest())
            return;

        /*
         * 必须在调用前清除标记。正常情况下官方函数不会返回；如果内部资源申请失败而
         * 异常返回，本次启动仍继续运行应用，也不会因RTC标记残留形成无限重启。
         */
        clearBootloaderRelayRequest();
        __atomic_store_n(&s_bootloaderAllowed, 0U, __ATOMIC_RELEASE);
        usb_persist_restart(RESTART_BOOTLOADER);
        __atomic_store_n(&s_bootloaderAllowed, 1U, __ATOMIC_RELEASE);
        Serial.println("[USB] 进入ROM下载器失败，设备已保持在应用模式，可重新尝试上传。");
    }

    /*
     * 【函数说明】把1200bps请求转成一次普通软件重启，由下一次最小启动进入ROM下载器。
     * 【资源隔离】这里只写RTC标记并撤销当前TinyUSB，不再让完整应用的堆状态参与PHY切换。
     */
    [[noreturn]] void restartForBootloaderRelay()
    {
        __atomic_store_n(&s_bootloaderAllowed, 0U, __ATOMIC_RELEASE);
        storeBootloaderRelayRequest();
        disconnectTinyUsb(0);
        ESP.restart();

        // ESP.restart()按约定不返回；保留循环满足编译器和异常平台实现的noreturn约束。
        while (true)
            delay(1000);
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
    // 实测必须返回 1 才能被 Windows 快速识别（提交 1ae3a00）；协议常理上的 0 会触发慢识别。
    return MSC_MAX_LUN;
}

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendorId[8],
                                   uint8_t productId[16], uint8_t productRevision[4])
{
    std::memset(vendorId, ' ', 8);
    std::memset(productId, ' ', 16);
    std::memset(productRevision, ' ', 4);
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return;
    }
    std::memcpy(vendorId, s_mscVendorId, std::min<size_t>(std::strlen(s_mscVendorId), 8));
    std::memcpy(productId, s_mscProductId, std::min<size_t>(std::strlen(s_mscProductId), 16));
    std::memcpy(productRevision, s_mscRevision, std::min<size_t>(std::strlen(s_mscRevision), 4));
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return false;
    }
    if (!s_mscMediaPresent)
    {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *blockCount, uint16_t *blockSize)
{
    if (!blockCount || !blockSize)
        return;
    if (!isSupportedMscLun(lun))
    {
        *blockCount = 0;
        *blockSize = 0;
        setUnsupportedLunSense(lun);
        return;
    }
    *blockCount = s_mscMediaPresent ? s_mscGeometry.blockCount : 0;
    *blockSize = s_mscMediaPresent ? s_mscGeometry.blockSize : 0;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t powerCondition, bool start, bool loadEject)
{
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return false;
    }
    return onMscStartStop(powerCondition, start, loadEject);
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                     void *buffer, uint32_t bufferSize)
{
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return -1;
    }
    return s_mscMediaPresent ? onMscRead(lba, offset, buffer, bufferSize) : -1;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                      uint8_t *buffer, uint32_t bufferSize)
{
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return -1;
    }
    return s_mscMediaPresent ? onMscWrite(lba, offset, buffer, bufferSize) : -1;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsiCommand[16],
                                   void *buffer, uint16_t bufferSize)
{
    (void)buffer;
    (void)bufferSize;
    if (!isSupportedMscLun(lun))
    {
        setUnsupportedLunSense(lun);
        return -1;
    }
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
        /*
         * 中继启动只需要CDC为官方PHY切换建立当前TinyUSB上下文。即使侧键仍被按住，
         * 也不打开FAT原始块后端、不注册MSC，确保进入ROM前保持最小资源状态。
         */
        const bool bootloaderRelayPending = hasBootloaderRelayRequest();
        if (bootloaderRelayPending)
            mode = Mode::CdcOnly;

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
        /*
         * 禁用Arduino Core在TinyUSB回调里的即时重启。普通模式的1200bps请求由onCdcEvent
         * 原子提交，再由主循环Service()完成干净断开；MSC模式则始终忽略下载请求，避免
         * Windows仍持有FAT写缓存时中断块设备会话。
         */
        s_cdc->enableReboot(false);
        s_cdc->onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onCdcEvent);
        s_cdc->onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, onCdcEvent);

        __atomic_store_n(&s_pendingEvents, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_readOps, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_writeOps, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_readBytes, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_writeBytes, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_ioErrors, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_hostAccessObserved, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_bootloaderRequested, 0U, __ATOMIC_RELAXED);
        __atomic_store_n(&s_bootloaderAllowed, 0U, __ATOMIC_RELAXED);

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
        /*
         * USB事件运行在Arduino事件任务，不直接读取主循环修改的s_mode/s_mscActive。
         * 只通过这个原子门控公布“当前可进入下载器”，保证MSC块设备在线时不会被中断。
         */
        __atomic_store_n(&s_bootloaderAllowed,
                         mode == Mode::CdcOnly ? 1U : 0U,
                         __ATOMIC_RELEASE);
        if (!USB.begin())
        {
            rollbackMsc();
            s_cdc->end();
            s_mode = Mode::CdcOnly;
            __atomic_store_n(&s_bootloaderAllowed, 0U, __ATOMIC_RELEASE);
            s_error = Error::UsbStartFailed;
            return false;
        }

        s_started = true;
        s_ejectRequested = false;
        if (bootloaderRelayPending)
            enterPendingRomBootloader();
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
        __atomic_store_n(&s_bootloaderAllowed, 1U, __ATOMIC_RELEASE);
        /*
         * USB描述符在本次启动内仍包含MSC接口，但介质和原始块后端都已经关闭。
         * 从这里开始允许onCdcEvent再次提交1200bps请求，正常固件无需为了恢复上传能力重启。
         */
        Serial.println("[FATFS][MSC] 原始块后端已关闭，PC的FAT访问权已释放，串口下载已恢复。");
    }

    void Service()
    {
        /*
         * IMU 纯脱线记录路径有意不调用 Begin()，因此主循环即使沿用统一 Service 调用点，
         * 也不能进入 USBCDC/TinyUSB。正常 CDC/MSC 启动成功后才补发缓存并消费事件。
         */
        if (!s_started)
            return;

        /*
         * 下载请求必须先于普通日志补发处理。1200bps touch已经关闭主机串口，
         * 复位链路不应再触碰CDC发送锁或TinyUSB TX状态。
         */
        if (__atomic_exchange_n(&s_bootloaderRequested, 0U, __ATOMIC_ACQ_REL) != 0 &&
            __atomic_load_n(&s_bootloaderAllowed, __ATOMIC_ACQUIRE) != 0)
            restartForBootloaderRelay();

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

        // 复用与下载器切换相同的撤销流程，确保软件重启和FAT更新也不会留下失效设备节点。
        disconnectTinyUsb(settleMs);
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
