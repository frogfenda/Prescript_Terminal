/*
【模块职责】开机 USB 选择、MSC 连接页、FAT 所有权切换以及弹出后的更新/重启编排。
【安全边界】MSC 活跃期间绝不挂载 FFat；只有收到可信安全弹出并关闭原始块后端后，ESP 才重新挂载 FAT。
*/
#include "sys/sys_usb_session.h"

#include "hal/hal.h"
#include "hal/hal_fat_storage.h"
#include "sys/sys_fat_update.h"
#include "sys/sys_usb_mode.h"

#include <Arduino.h>

namespace
{
    void drawCentered(const String &text, int y, HALFontRole role, uint16_t color)
    {
        const int width = HAL_Get_Text_Width_Font(text.c_str(), role);
        HAL_Screen_ShowLine_Font((HAL_Get_Screen_Width() - width) / 2, y, text.c_str(), role, color);
    }

    void showConnectionPage()
    {
        HAL_Init();
        HAL_Sprite_Clear();
        drawCentered("USB 存储模式", 8, HAL_FONT_TITLE, TFT_CYAN);
        drawCentered("FATFS 已连接电脑", 40, HAL_FONT_BODY, TFT_GREEN);
        drawCentered("可访问 /Update 与 /Resources", 72, HAL_FONT_SMALL, TFT_WHITE);
        drawCentered("完成后请在电脑安全弹出 U 盘", 108, HAL_FONT_BODY, TFT_YELLOW);
        HAL_Screen_Update();
    }

    void showStatusPage(const String &title, const String &detail, uint16_t color)
    {
        HAL_Init();
        HAL_Sprite_Clear();
        drawCentered(title, 24, HAL_FONT_TITLE, color);
        drawCentered(detail, 67, HAL_FONT_BODY, TFT_WHITE);
        HAL_Screen_Update();
    }

    [[noreturn]] void haltWithUsbService()
    {
        while (true)
        {
            SysUsbMode::Service();
            delay(10);
        }
    }

    void runExclusiveMscSession(bool restartIntoOfflineRecorder)
    {
        showConnectionPage();

        while (!SysUsbMode::ConsumeEjectRequest())
        {
            SysUsbMode::Service();
            delay(10);
        }

        Serial.println("[FATFS][MSC] 已安全弹出，正在切换为设备端文件访问。");
        showStatusPage("U 盘已安全弹出",
                       restartIntoOfflineRecorder ? "正在关闭 USB" : "正在检查 /Update",
                       TFT_CYAN);
        SysUsbMode::StopMsc();

        if (restartIntoOfflineRecorder)
        {
            /*
             * 脱线采集一旦启动过 TinyUSB，最高优先级 usbd 任务会持续存在到重启。
             * 不能在同一次 MSC 会话后直接进入记录器，否则导出后的下一份数据仍可能出现
             * 周期采样空洞。先把 FAT 交还 ESP，并复用现有拖拽更新入口检查 /Update：
             * 用户仍可在导出时放入普通固件 firmware.bin，安全弹出后完成恢复。没有更新包时
             * 再卸载 FAT、主动断开并重启；侧键已经释放，下一次启动会走无 USB 记录路径。
             */
            if (!HAL::FatStorage::MountForEsp())
            {
                showStatusPage("导出后处理失败", "FATFS 无法交还设备", TFT_RED);
                haltWithUsbService();
            }

            const SysFatUpdate::BootResult updateResult = SysFatUpdate::CheckAndApplyAtBoot();
            if (updateResult != SysFatUpdate::BootResult::NoPackage)
            {
                // 成功更新会在现有接口内部重启；失败则保留错误页和更新文件，等待用户重新进入 MSC。
                haltWithUsbService();
            }

            HAL::FatStorage::UnmountFromEsp();
            showStatusPage("导出完成", "正在重启脱线采集", TFT_GREEN);
            SysUsbMode::DisconnectBeforeRestart();
            ESP.restart();
            haltWithUsbService();
        }

        // 不在这里重新挂载或重启：普通启动流程会统一挂载 FAT、扫描 /Update，
        // 无更新包时直接继续加载应用，避免 CDC 端口因模式切换被 Windows 换号。
        showStatusPage("MSC 模式已结束", "正在加载系统资源", TFT_GREEN);
    }
}

namespace SysUsbSession
{
    void BeginAndHandleBootMode(bool bootTestEnabled, bool allowMscForBootTest)
    {
        const bool forceCdcOnly = bootTestEnabled && !allowMscForBootTest;
        const SysUsbMode::Mode mode = forceCdcOnly ? SysUsbMode::Mode::CdcOnly
                                                   : SysUsbMode::DecideFromBtn2();

        const bool offlineRecorderBoot = bootTestEnabled && allowMscForBootTest;
        if (offlineRecorderBoot && mode == SysUsbMode::Mode::CdcOnly)
        {
            /*
             * 当前允许 MSC 的隔离测试只有 IMU 脱线记录器。未按侧键表示本次启动要记录数据，
             * 因而必须在 USB.begin() 之前返回：仅把 CDC 锁等待改成 0 ms 仍会创建最高优先级
             * TinyUSB usbd 任务，第二批实测仍每约 450～460 ms 出现一次 40 ms 空洞。
             * 记录器继续使用屏幕、旋钮、按键和 FAT；串口控制在纯脱线启动中有意不可用。
             */
            return;
        }

        // MSC 必须走最短枚举路径。/Update 已在普通启动和上一次安全弹出后的扫描中
        // 自动创建；这里不允许为了检查目录而先执行耗时的 FFat 挂载。
        SysUsbMode::Config usbConfig = {};
        if (bootTestEnabled)
        {
            /*
             * 隔离测试的主循环可能承担 104 Hz 传感器采样，不能等待 CDC 发送锁。
             * Arduino-ESP32 的 availableForWrite() 与 flush() 各自最多等待一次
             * cdcTxTimeoutMs；正常固件保留默认 20 ms 以提高日志送达率，而测试固件
             * 使用 0 ms 只做机会式补发。CDC RX 由 TinyUSB 回调进入队列，不依赖这个
             * 发送超时，因此串口控制命令和 MSC 独占会话仍可照常工作。
             */
            usbConfig.cdcTxTimeoutMs = 0;
        }
        const bool usbStarted = SysUsbMode::Begin(mode, usbConfig);
        if (forceCdcOnly)
            return;

        if (usbStarted)
            Serial.println("[系统] TinyUSB CDC 已启动。");
        else
            Serial.printf("[系统] TinyUSB 启动失败：%s。\n", SysUsbMode::LastErrorText());

        if (mode != SysUsbMode::Mode::CdcWithMsc)
            return;

        if (!usbStarted || !SysUsbMode::IsMscActive())
        {
            showStatusPage("MSC 启动失败", SysUsbMode::LastErrorText(), TFT_RED);
            haltWithUsbService();
        }

        runExclusiveMscSession(offlineRecorderBoot);
    }
}
