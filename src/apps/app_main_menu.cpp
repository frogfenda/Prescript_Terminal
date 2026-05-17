/*
【模块职责】主菜单。把 12 个顶层功能入口映射到 AppId 跳转，包括指令、纺织机、日程、闹钟、TT2、番茄、硬币、抽卡、档案和设置。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_main_menu.cpp
#include "app_menu_base.h"

class AppMainMenu : public AppMenuBase
{
protected:
    // 【函数说明】返回 12 个主菜单入口，数量必须与 getItemText/onItemClicked 的 switch 保持一致。
    int getMenuCount() override { return 12; } // 菜单数组有 12 项，避免最后一项滚不到

    const char *getTitle() override
    {
        return (appManager.getLanguage() == LANG_ZH) ? "都市主控菜单" : "MAIN MENU";
    }

    const char *getItemText(int index) override
    {
        if (appManager.getLanguage() == LANG_ZH)
        {
            // 【修改 2】：在硬币和档案之间插入了“提取部模拟”
            const char *items[] = {"接受指令", "纺织机", "定时指令", "但丁", "TT2协议", "专注协议", "硬币决定器", "提取部模拟", "指令档案", "指令推送配置", "系统高级设置", "进入待机模式"};
            return items[index];
        }
        else
        {
            const char *items[] = {"RECEIVE PRESCRIPT", "LOOM", "SCHEDULES", "WAKEUP ALARM", "TT2 PROTOCOL", "POMODORO TIMER", "QUANTUM COIN", "EXTRACTION SIM", "PRESCRIPT DB", "PUSH SETTINGS", "SYSTEM SETTINGS", "STANDBY MODE"};
            return items[index];
        }
    }

// 【函数说明】把主菜单 index 映射到 AppId：进入指令、日程、闹钟、TT2、番茄、硬币、抽卡、档案和设置。
void onItemClicked(int index) override
    {
        if (index == 0)
            appManager.push(AppId::Prescript);
        else if (index == 1)
            appManager.push(AppId::Loom);
        else if (index == 2)
            appManager.push(AppId::Schedule);
        else if (index == 3)
            appManager.push(AppId::Alarm);
        else if (index == 4)
            appManager.push(AppId::Countdown); 
        else if (index == 5)
            appManager.push(AppId::Pomodoro);
        else if (index == 6)
            appManager.push(AppId::CoinFlip);
        else if (index == 7)
            appManager.push(AppId::Gacha); // 路由到抽卡模拟器
        else if (index == 8)
            appManager.push(AppId::PrescriptList);
        else if (index == 9)
            appManager.push(AppId::PushSetting);
        else if (index == 10)
            appManager.push(AppId::SystemSettings);
        else if (index == 11)
            appManager.launch(AppId::Standby);
    }

    // 【函数说明】主菜单长按直接进入待机页，等同于手动让终端休眠。
    void onLongPressed() override { appManager.launch(AppId::Standby); }
};

AppMainMenu instanceMainMenu;
AppBase *appMainMenu = &instanceMainMenu;
