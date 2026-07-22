#pragma once

#include <Arduino.h>
#include <USBCDC.h>

/*
【模块职责】ESP32-S3 TinyUSB 启动决策与 CDC/MSC 复合设备初始化。

- BTN2 未按住：只启动 CDC，不注册 MSC 接口。
- BTN2 按住：启动 CDC + 可用 MSC；MSC 块存储由 HAL::FatStorage 提供。
- 所有接口都在唯一一次 USB.begin() 前注册，确保 USB 描述符稳定。
- 复合模式使用标准顺序：MSC 为接口 0，CDC 为接口 1/2，优先枚举磁盘功能。

本模块采用惰性对象创建；只要上层不调用 Begin()/BeginFromBtn2()，就不会注册
TinyUSB 接口、读取按键、挂载 FAT 后端或启动 USB。
*/
namespace SysUsbMode
{
    enum class Mode : uint8_t
    {
        CdcOnly,
        CdcWithMsc,
    };

    enum class Error : uint8_t
    {
        None,
        HardwareUsbModeEnabled,
        AutomaticUsbStartupEnabled,
        AlreadyStartedWithDifferentMode,
        FatBackendUnavailable,
        AllocationFailed,
        MscConfigurationFailed,
        UsbStartFailed,
    };

    struct Config
    {
        uint16_t usbVid = 0x303A;
        // CDC-only 与 CDC+MSC 使用同一设备实例；CDC 永远位于接口 0/1，避免 COM 号随模式切换。
        uint16_t usbPid = 0x0002;
        // 复合描述符布局改变后递增版本，帮助 Windows 重新读取接口树。
        uint16_t usbFirmwareVersion = 0x0102;
        uint32_t cdcBaud = 115200;
        size_t cdcRxBufferSize = 1024;
        uint32_t cdcTxTimeoutMs = 20;
        const char *manufacturer = "Fogfenda";
        const char *productName = "Prescript Terminal";
        // 两种模式使用同一个芯片 MAC 序列号，确保 Windows 复用同一个设备实例。
        const char *serialNumber = "__MAC__";
        const char *mscVendorId = "FOGFENDA";
        const char *mscProductId = "ESP32S3 FAT";
        const char *mscRevision = "1.0";
        const char *fatPartitionLabel = "fatfs";
    };

    // 读取 BTN2（BSP::Pins::BTN_SIDE，低电平按下）并锁定本次启动模式。
    Mode DecideFromBtn2(uint32_t settleMs = 25, uint8_t samples = 5, uint32_t sampleGapMs = 2);

    // 初始化 CDC 或 CDC+MSC，并在全部接口注册后调用一次 USB.begin()。
    bool Begin(Mode mode, const Config &config = Config{});

    // DecideFromBtn2() + Begin() 的便捷入口；当前未接入 main.cpp。
    bool BeginFromBtn2(const Config &config = Config{});

    // MSC 安全下线并释放 FAT 原始块后端；CDC 和全局 TinyUSB 保持运行。
    // 仅应在 ConsumeEjectRequest() 返回 true 后调用，避免丢弃电脑尚未刷新的 FAT 缓存。
    void StopMsc();

    // 补发主机连接前缓存的 CDC 日志；应由主循环持续调用。
    void Service();

    // 软件重启前主动撤销 USB 上拉并保留稳定断开窗口，避免 Windows 留下失效复合设备实例。
    // 调用后只允许执行 ESP.restart()，不可继续正常业务。
    void DisconnectBeforeRestart(uint32_t settleMs = 2000);

    bool IsStarted();
    bool IsMscActive();
    Mode CurrentMode();
    Error LastError();
    const char *LastErrorText();

    // 手动创建的 USB CDC 对象。Begin() 成功前返回 nullptr。
    USBCDC *Cdc();

    // Windows 安全弹出（START STOP UNIT: loadEject=true, start=false）后置位；
    // 不依赖拔线，读取后自动清零，由独占 MSC 会话立即检查更新并重启。
    bool ConsumeEjectRequest();
}
