/*
【模块职责】TinyUSB CDC/MSC 接口注册、BTN2模式决策以及MSC生命周期协调。
【边界】FAT文件系统与块读写细节全部委托给 HAL::FatStorage。
*/
#include "sys/sys_usb_mode.h"

#include "bsp/bsp_pins.h"
#include "hal/hal_fat_storage.h"

#include <USB.h>
#include <USBMSC.h>
#include <new>

namespace
{
    USBCDC *s_cdc = nullptr;
    USBMSC *s_msc = nullptr;
    SysUsbMode::Mode s_mode = SysUsbMode::Mode::CdcOnly;
    SysUsbMode::Error s_error = SysUsbMode::Error::None;
    bool s_started = false;
    bool s_mscActive = false;
    volatile bool s_ejectRequested = false;

    int32_t onMscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufferSize)
    {
        return HAL::FatStorage::Read(lba, offset, buffer, bufferSize);
    }

    int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufferSize)
    {
        return HAL::FatStorage::Write(lba, offset, buffer, bufferSize);
    }

    bool onMscStartStop(uint8_t powerCondition, bool start, bool loadEject)
    {
        (void)powerCondition;
        if (loadEject && !start)
            s_ejectRequested = true;
        return true;
    }

    void rollbackMsc()
    {
        if (s_msc)
        {
            s_msc->mediaPresent(false);
            s_msc->end();
        }
        s_mscActive = false;
        HAL::FatStorage::CloseForUsb();
    }
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

        if (!s_cdc)
            s_cdc = new (std::nothrow) USBCDC(0);
        if (!s_cdc)
        {
            HAL::FatStorage::CloseForUsb();
            s_error = Error::AllocationFailed;
            return false;
        }

        s_cdc->begin(config.cdcBaud);

        if (mode == Mode::CdcWithMsc)
        {
            if (!s_msc)
                s_msc = new (std::nothrow) USBMSC();
            if (!s_msc)
            {
                HAL::FatStorage::CloseForUsb();
                s_error = Error::AllocationFailed;
                return false;
            }

            const HAL::FatStorage::Geometry geometry = HAL::FatStorage::GetGeometry();
            s_msc->mediaPresent(false);
            s_msc->vendorID(config.mscVendorId ? config.mscVendorId : "FOGFENDA");
            s_msc->productID(config.mscProductId ? config.mscProductId : "ESP32S3 FAT");
            s_msc->productRevision(config.mscRevision ? config.mscRevision : "1.0");
            s_msc->onRead(onMscRead);
            s_msc->onWrite(onMscWrite);
            s_msc->onStartStop(onMscStartStop);

            if (!s_msc->begin(geometry.blockCount, geometry.blockSize))
            {
                rollbackMsc();
                s_error = Error::MscConfigurationFailed;
                return false;
            }

            s_msc->mediaPresent(true);
            s_mscActive = true;
        }

        if (config.manufacturer)
            USB.manufacturerName(config.manufacturer);
        if (config.productName)
            USB.productName(config.productName);
        if (config.serialNumber)
            USB.serialNumber(config.serialNumber);

        if (!USB.begin())
        {
            rollbackMsc();
            s_cdc->end();
            s_error = Error::UsbStartFailed;
            return false;
        }

        s_mode = mode;
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
        rollbackMsc();
        if (s_mode == Mode::CdcWithMsc)
            s_mode = Mode::CdcOnly;
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
