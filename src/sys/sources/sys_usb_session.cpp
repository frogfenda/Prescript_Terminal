/*
【模块职责】开机 USB 选择、MSC 连接页、FAT 所有权切换以及弹出后的更新/重启编排。
【安全边界】MSC 活跃期间绝不挂载 FFat；只有收到可信安全弹出并关闭原始块后端后，ESP 才重新挂载 FAT。
*/
#include "sys/sys_usb_session.h"

#include "hal/hal.h"
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
        drawCentered("可将更新镜像拖入 /Update", 72, HAL_FONT_SMALL, TFT_WHITE);
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

    void runExclusiveMscSession()
    {
        showConnectionPage();

        while (!SysUsbMode::ConsumeEjectRequest())
        {
            SysUsbMode::Service();
            delay(10);
        }

        Serial.println("[FATFS][MSC] 已安全弹出，正在切换为设备端文件访问。");
        showStatusPage("U 盘已安全弹出", "正在检查 /Update", TFT_CYAN);
        SysUsbMode::StopMsc();
        // 不在这里重新挂载或重启：普通启动流程会统一挂载 FAT、扫描 /Update，
        // 无更新包时直接继续加载应用，避免 CDC 端口因模式切换被 Windows 换号。
        showStatusPage("MSC 模式已结束", "正在加载系统资源", TFT_GREEN);
    }
}

namespace SysUsbSession
{
    void BeginAndHandleBootMode(bool bootTestEnabled)
    {
        const SysUsbMode::Mode mode = bootTestEnabled ? SysUsbMode::Mode::CdcOnly
                                                       : SysUsbMode::DecideFromBtn2();

        // MSC 必须走最短枚举路径。/Update 已在普通启动和上一次安全弹出后的扫描中
        // 自动创建；这里不允许为了检查目录而先执行耗时的 FFat 挂载。
        const bool usbStarted = SysUsbMode::Begin(mode);
        if (bootTestEnabled)
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

        runExclusiveMscSession();
    }
}
