// 文件：src/app_network_sync.cpp
#include "app_base.h"
#include "app_manager.h"

class AppNetworkSync : public AppBase
{
private:
    uint32_t start_time;

    void drawUI()
    {
        HAL_Screen_Clear();
        HAL_Screen_DrawHeader();

        // 【解耦 1】：获取屏幕动态宽高
        int sw = HAL_Get_Screen_Width();
        int sh = HAL_Get_Screen_Height();

        const char* text = (appManager.getLanguage() == LANG_ZH) ? "网络未配置..." : "Network Offline...";
        
        // 【解耦 2】：通过文字宽度测算，实现全屏绝对居中
        int text_x = (sw - HAL_Get_Text_Width(text)) / 2;
        int text_y = sh / 2 - 8; // 减去 8 是为了让 16px 字体垂直居中

        HAL_Screen_ShowChineseLine(text_x, text_y, text);
        HAL_Screen_Update();
    }

public:
    void onCreate() override
    {
        drawUI();
        start_time = millis();
    }

    void onLoop() override
    {
        if (millis() - start_time > 1500)
            appManager.popApp(); 
    }
    void onDestroy() override {}
    void onKnob(int delta) override {}
    void onKeyShort() override { appManager.popApp(); } 
    void onKeyLong() override { appManager.popApp(); }
};

AppNetworkSync instanceNetworkSync;
AppBase *appNetworkSync = &instanceNetworkSync;