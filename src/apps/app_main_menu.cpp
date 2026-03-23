// 文件：src/apps/app_main_menu.cpp
#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase
{
protected:
    int getMenuCount() override { return 10; } // 【修改 1】：增加到 10 个

    const char *getTitle() override
    {
        return (appManager.getLanguage() == LANG_ZH) ? "都市主控菜单" : "MAIN MENU";
    }

    const char *getItemText(int index) override
    {
        if (appManager.getLanguage() == LANG_ZH)
        {
            // 在但丁(闹钟)和专注协议之间插入了“倒计时”
            const char *items[] = {"接受指令", "定时指令", "但丁", "TT2协议", "专注协议", "硬币决定器", "指令档案", "指令推送配置", "系统高级设置", "进入待机模式"};
            return items[index];
        }
        else
        {
            const char *items[] = {"RECEIVE PRESCRIPT", "SCHEDULES", "WAKEUP ALARM", "TT2 PROTOCOL", "POMODORO TIMER", "QUANTUM COIN", "PRESCRIPT DB", "PUSH SETTINGS", "SYSTEM SETTINGS", "STANDBY MODE"};
            return items[index];
        }
    }

    void onItemClicked(int index) override
    {
        if (index == 0)
            appManager.pushApp(appPrescript);
        else if (index == 1)
            appManager.pushApp(appSchedule);
        else if (index == 2)
            appManager.pushApp(appAlarm);
        else if (index == 3)
            appManager.pushApp(appCountdown); // <--- 我们刚写的倒计时！
        else if (index == 4)
            appManager.pushApp(appPomodoro);
        else if (index == 5)
            appManager.pushApp(appCoinFlip);
        else if (index == 6)
            appManager.pushApp(appPrescriptList);
        else if (index == 7)
            appManager.pushApp(appPushSetting);
        else if (index == 8)
            appManager.pushApp(appSystemSettings);
        else if (index == 9)
            appManager.launchApp(appStandby);
    }

    void onLongPressed() override { appManager.launchApp(appStandby); }
};

AppMainMenu instanceMainMenu;
AppBase *appMainMenu = &instanceMainMenu;