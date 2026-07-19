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

    [[noreturn]] void restartAfterMscExit()
    {
        SysUsbMode::RequestCdcOnlyOnNextBoot();
        Serial.println("[FATFS][MSC] 会话结束，重启以重新加载全部资源。");
        SysUsbMode::Service();
        SysUsbMode::DisconnectBeforeRestart();
        ESP.restart();
        while (true)
            delay(1000);
    }

    [[noreturn]] void runExclusiveMscSession()
    {
        showConnectionPage();
        const HAL::FatStorage::Geometry geometry = HAL::FatStorage::GetGeometry();
        Serial.printf("[FATFS][MSC] PC 已取得 FAT 独占访问权：%lu blocks x %u bytes。\n",
                      static_cast<unsigned long>(geometry.blockCount),
                      static_cast<unsigned>(geometry.blockSize));
        Serial.println("[FATFS][MSC] 普通文件系统、配置、网络和主程序初始化均已暂停。");

        while (!SysUsbMode::ConsumeEjectRequest())
        {
            SysUsbMode::Service();
            delay(10);
        }

        Serial.println("[FATFS][MSC] 收到可信安全弹出请求，停止 MSC 块访问。");
        showStatusPage("U 盘已安全弹出", "正在检查 /Update", TFT_CYAN);
        SysUsbMode::StopMsc();
        delay(100);

        if (!HAL::FatStorage::MountForEsp())
        {
            Serial.println("[FATFS][错误] 弹出后无法挂载 FATFS；本次不执行文件读取。");
            showStatusPage("FATFS 挂载失败", "设备即将重启", TFT_RED);
            delay(1000);
            restartAfterMscExit();
        }

        Serial.println("[FATFS] 安全弹出完成，ESP 已取得 FAT 独占访问权：/fat。");
        // 更新成功会在检查函数内部直接重启，因此扫描前预置一次性正常启动标记。
        SysUsbMode::RequestCdcOnlyOnNextBoot();
        const SysFatUpdate::BootResult updateResult = SysFatUpdate::CheckAndApplyAtBoot();
        if (updateResult == SysFatUpdate::BootResult::Failed)
        {
            SysUsbMode::CancelCdcOnlyOnNextBoot();
            Serial.println("[FATFS][UPDATE] 更新失败，保留更新文件并停止启动；请复位后进入 MSC 修正文件。");
            haltWithUsbService();
        }

        // 更新成功时检查函数已重启；没有更新包时也统一执行干净重启。
        showStatusPage("MSC 模式已结束", "正在重启并加载资源", TFT_GREEN);
        HAL::FatStorage::UnmountFromEsp();
        restartAfterMscExit();
    }
}

namespace SysUsbSession
{
    void BeginAndHandleBootMode(bool bootTestEnabled)
    {
        const bool forceCdcOnly = SysUsbMode::ConsumeCdcOnlyOnNextBootRequest();
        const SysUsbMode::Mode mode = (bootTestEnabled || forceCdcOnly)
                                          ? SysUsbMode::Mode::CdcOnly
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

        if (forceCdcOnly)
            Serial.println("[FATFS][MSC] 已消费退出标记，本次强制正常启动并重新加载资源。");

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
