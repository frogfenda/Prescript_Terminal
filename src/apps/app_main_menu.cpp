// 文件：src/apps/app_main_menu.cpp
#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { return 11; } // 菜单数组有 11 项，避免最后一项滚不到

    const char *getTitle() override
    {
        return (appManager.getLanguage() == LANG_ZH) ? "都市主控菜单" : "MAIN MENU";
    }

    const char *getItemText(int index) override
    {
        if (appManager.getLanguage() == LANG_ZH)
        {
            // 【修改 2】：在硬币和档案之间插入了“提取部模拟”
            const char *items[] = {"接受指令", "定时指令", "但丁", "TT2协议", "专注协议", "硬币决定器", "提取部模拟", "指令档案", "指令推送配置", "系统高级设置", "进入待机模式"};
            return items[index];
        }
        else
        {
            const char *items[] = {"RECEIVE PRESCRIPT", "SCHEDULES", "WAKEUP ALARM", "TT2 PROTOCOL", "POMODORO TIMER", "QUANTUM COIN", "EXTRACTION SIM", "PRESCRIPT DB", "PUSH SETTINGS", "SYSTEM SETTINGS", "STANDBY MODE"};
            return items[index];
        }
    }

void onItemClicked(int index) override
    {
        if (index == 0)
            appManager.push(AppId::Prescript);
        else if (index == 1)
            appManager.push(AppId::Schedule);
        else if (index == 2)
            appManager.push(AppId::Alarm);
        else if (index == 3)
            appManager.push(AppId::Countdown); 
        else if (index == 4)
            appManager.push(AppId::Pomodoro);
        else if (index == 5)
            appManager.push(AppId::CoinFlip);
        else if (index == 6)
            appManager.push(AppId::Gacha); // <--- 【修改 3】：路由到抽卡模拟器
        else if (index == 7)
            appManager.push(AppId::PrescriptList);
        else if (index == 8)
            appManager.push(AppId::PushSetting);
        else if (index == 9)
            appManager.push(AppId::SystemSettings);
        else if (index == 10)
            appManager.launch(AppId::Standby);
    }

    void onLongPressed() override { appManager.launch(AppId::Standby); }
};

AppMainMenu instanceMainMenu;
AppBase *appMainMenu = &instanceMainMenu;