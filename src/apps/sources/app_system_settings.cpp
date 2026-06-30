/*
【模块职责】系统高级设置菜单。

本页面是设备系统类功能的入口，使用 AppMenuBase 的滚轮菜单 UI。
本补丁在原有设置项中插入“时间设置 / TIME CONFIG”，让用户可以：
- 手动设置当日时分；
- 进入完整网络同步页执行 NTP + API；
- 开关周期校时；
- 调整周期校时间隔。

交互规则：
- 短按：进入当前选中设置项；
- 长按：返回上一级。
*/
#include "apps/app_menu_base.h"
#include "sys/sys_config.h"
#include "sys/sys_network.h"
#include "lang/ui_strings.h"
#include "sys/sys_haptic.h"

class AppSystemSettings : public AppMenuBase {
protected:
    /**
     * 系统设置共 9 项。
     * 新增的“时间设置”放在网络同步之后，逻辑上属于网络时间的下一级详细配置。
     */
    int getMenuCount() override { return 9; }

    /** 返回系统设置页标题，使用已有 UIStrings 适配中英文。 */
    const char* getTitle() override {
        return UIStrings::SystemSettingsTitle(appManager.getLanguage());
    }

    /**
     * 返回滚轮菜单每一项的显示文本。
     *
     * 第 0 项 WiFi 会根据 Network_GetState() 动态显示：
     * - 已连接：显示断开；
     * - 连接/NTP/API 中：显示网络运行中；
     * - 其他状态：显示连接无线网络。
     *
     * 第 2 项是本补丁新增的时间设置入口。
     */
    const char* getItemText(int index) override {
        NetworkState state = Network_GetState();
        SystemLang_t lang = appManager.getLanguage();

        if (index == 0) {
            if (state == NET_SYNC_SUCCESS) return UIStrings::WifiDisconnectItem(lang);
            if (state == NET_CONNECTING || state == NET_SYNCING_NTP || state == NET_FETCHING_API) return UIStrings::WifiBusyItem(lang);
            return UIStrings::WifiConnectItem(lang);
        }

        return UIStrings::SystemSettingsItem(lang, index);
    }

    /**
     * 处理系统设置菜单短按。
     *
     * 映射关系：
     * 0 WiFi 连接/断开；
     * 1 完整网络同步页；
     * 2 时间设置页；
     * 3 抽卡统计；
     * 4 语言切换或锁定语言提示；
     * 5 休眠；
     * 6 音量震动；
     * 7 解码动画；
     * 8 返回。
     */
    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::WifiConnect);
        else if (index == 1) appManager.push(AppId::NetworkSync);
        else if (index == 2) appManager.push(AppId::TimeSetting);
        else if (index == 3) appManager.push(AppId::GachaStats);
        else if (index == 4) {
            if (appManager.isLanguageLocked()) {
                SYS_HAPTIC_BACK();
                drawMenuUI(visual_selection);
                return;
            }
            appManager.toggleLanguage();
            drawMenuUI(visual_selection);
        }
        else if (index == 5) appManager.push(AppId::SleepSetting);
        else if (index == 6) appManager.push(AppId::VolumeSetting);
        else if (index == 7) appManager.push(AppId::AnimSetting);
        else if (index == 8) appManager.popApp();
    }

    /** 长按退出系统设置页，回到上一级菜单。 */
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;
