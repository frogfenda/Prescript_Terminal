#pragma once

#include <Arduino.h>
#include <USBCDC.h>

/*
【模块职责】ESP32-S3 TinyUSB 启动决策与 CDC/MSC 复合设备初始化。

- BTN2 未按住：只启动 CDC，不注册 MSC 接口。
- BTN2 按住：启动 CDC + 可用 MSC；MSC 块存储由 HAL::FatStorage 提供。
- 所有接口都在唯一一次 USB.begin() 前注册，确保 USB 描述符稳定。
- CDC 固定为 MI_00；复合模式将标准 MSC 描述符后置为接口 2。

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
        uint16_t usbPid = 0x0002;
        uint32_t cdcBaud = 115200;
        size_t cdcRxBufferSize = 1024;
        uint32_t cdcTxTimeoutMs = 20;
        const char *manufacturer = "Fogfenda";
        const char *productName = "Prescript Terminal";
        // 两种模式都使用同一个芯片 MAC 序列号，配合固定 VID/PID 与 CDC MI_00 保持 COM 号。
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

    // MSC 退出重启使用的一次性 RTC 标记。即使 BTN2 尚未释放，下一次也强制 CDC-only，
    // 确保资源在全新的普通启动周期中加载；更新失败时可取消标记以便再次进入 MSC 修复。
    void RequestCdcOnlyOnNextBoot();
    void CancelCdcOnlyOnNextBoot();
    bool ConsumeCdcOnlyOnNextBootRequest();

    // MSC 安全下线并释放 FAT 原始块后端；CDC 和全局 TinyUSB 保持运行。
    // 仅应在 ConsumeEjectRequest() 返回 true 后调用，避免丢弃电脑尚未刷新的 FAT 缓存。
    void StopMsc();

    // 补发主机连接前缓存的 CDC 日志；应由主循环持续调用。
    void Service();

    // 软件重启前主动撤销 USB 上拉并保留稳定断开窗口，避免 Windows 留下失效复合设备实例。
    // 调用后只允许执行 ESP.restart()，不可继续正常业务。
    void DisconnectBeforeRestart(uint32_t settleMs = 800);

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
