/*
【模块职责】设备端账号认领模式入口。

设备不再直接访问绑定接口，也不在屏幕上输入账号。用户明确按键进入一次性绑定模式后，
App 才能通过 BLE 读取机器码、向账号服务器申请挑战、取得设备证明并写回 DEV_BIND_V2。
模式状态保存在 RAM，超时由 SysDeviceBinding_Update 关闭。
*/
#include "sys/app_base.h"
#include "sys/app_manager.h"
#include "sys/sys_device_binding.h"
#include "sys/sys_ble.h"
#include "sys/sys_audio.h"
#include "lang/ui_strings.h"
#include "ui/ui_frame.h"

class AppDeviceBinding : public AppBase
{
    bool active_ = false;

    void drawPage()
    {
        const SystemLang_t lang = appManager.getLanguage();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::DeviceBindingTitle(lang));

        const bool mode_active = active_ && SysDeviceBinding_IsModeActive();
        const char *message = mode_active
                                  ? UIStrings::DeviceBindingRunning(lang)
                                  : UIStrings::DeviceBindingPrompt(lang);
        const char *hint = mode_active
                               ? UIStrings::DeviceBindingWaitingHint(lang)
                               : UIStrings::DeviceBindingReadyHint(lang);
        const uint16_t color = mode_active ? TFT_GREEN : TFT_CYAN;
        const int sw = HAL_Get_Screen_Width();
        const int sh = HAL_Get_Screen_Height();
        const int y = sh / 2 - HAL_Get_Font_Line_Height(HAL_FONT_BODY) / 2;
        const int x = (sw - HAL_Get_Text_Width(message)) / 2;
        HAL_Screen_ShowLine_Font(x, y, message, HAL_FONT_BODY, color);
        UIFrame::DrawTip(hint);
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        // 进入页面不继承上一次残留状态；绑定模式只能由本次页面的主键明确开启。
        // 退出时若原本处于绑定模式，SysDeviceBinding_ExitMode 会推送 EVT:BIND.MODE。
        SysDeviceBinding_ExitMode();
        active_ = false;
        drawPage();
    }

    void onLoop() override
    {
        if (active_ && !SysDeviceBinding_IsModeActive())
        {
            active_ = false;
            drawPage();
        }
    }

    void onDestroy() override
    {
        // 所有离开路径最终都会经过 onDestroy；这里是绑定模式唯一的生命周期兜底。
        if (SysDeviceBinding_IsModeActive())
            SysDeviceBinding_ExitMode();
        active_ = false;
    }
    void onKnob(int) override {}

    void onKeyShort() override
    {
        if (SysDeviceBinding_IsModeActive())
            return;
        SysDeviceBinding_EnterMode();
        active_ = true;
        sysAudio.playTone(1500, 100);
        drawPage();
    }

    void onKeyLong() override
    {
        appManager.popApp();
    }

    void onBtn2Short() override {}
    void onBtn2Long() override { appManager.popApp(); }
};

AppDeviceBinding instanceDeviceBinding;
AppBase *appDeviceBinding = &instanceDeviceBinding;

/*
【模块职责】设备实体 BLE 配对页。

该页面和上面的账号绑定页完全分开：短按会先断开旧手机、清除旧 bonding，再开启一次性 120 秒配对窗口。
只有从这里进入的窗口才接受新手机；配对成功后窗口自动关闭，设备始终只保留最新一台手机的密钥。
*/
class AppBlePairing : public AppBase
{
    bool active_ = false;

    void drawPage()
    {
        const SystemLang_t lang = appManager.getLanguage();
        HAL_Sprite_Clear();
        drawAppWindow(UIStrings::BlePairingTitle(lang));

        const bool mode_active = active_ && SysBLE_IsPairingMode();
        const char *message = mode_active
                                  ? UIStrings::BlePairingRunning(lang)
                                  : UIStrings::BlePairingPrompt(lang);
        const char *hint = mode_active
                               ? UIStrings::BlePairingWaitingHint(lang)
                               : UIStrings::BlePairingReadyHint(lang);
        const uint16_t color = mode_active ? TFT_GREEN : TFT_CYAN;
        const int sw = HAL_Get_Screen_Width();
        const int sh = HAL_Get_Screen_Height();
        const int y = sh / 2 - HAL_Get_Font_Line_Height(HAL_FONT_BODY) / 2;
        const int x = (sw - HAL_Get_Text_Width(message)) / 2;
        HAL_Screen_ShowLine_Font(x, y, message, HAL_FONT_BODY, color);
        UIFrame::DrawTip(hint);
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        // 每次重新进入页面都要求用户再次短按确认，避免误触上一次残留的配对窗口。
        SysBLE_ExitPairingMode();
        active_ = false;
        drawPage();
    }

    void onLoop() override
    {
        if (active_ && !SysBLE_IsPairingMode())
        {
            active_ = false;
            drawPage();
        }
    }

    void onDestroy() override
    {
        if (SysBLE_IsPairingMode())
            SysBLE_ExitPairingMode();
        active_ = false;
    }

    void onKnob(int) override {}

    void onKeyShort() override
    {
        if (active_ || SysBLE_IsPairingMode())
            return;
        // true 表示替换旧手机：断开当前连接并清空旧 bonding 后再开窗口。
        SysBLE_EnterPairingMode(true);
        active_ = true;
        sysAudio.playTone(1500, 100);
        drawPage();
    }

    void onKeyLong() override { appManager.popApp(); }
    void onBtn2Short() override {}
    void onBtn2Long() override { appManager.popApp(); }
};

AppBlePairing instanceBlePairing;
AppBase *appBlePairing = &instanceBlePairing;
