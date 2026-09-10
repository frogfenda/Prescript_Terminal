/*
【模块职责】系统高级设置菜单。

本页面是设备系统类功能的入口，使用 AppMenuBase 的滚轮菜单 UI。
系统设置只保留设备级设置入口；动作、坐标漂移和地磁校准统一归入
“传感器校准测试”二级菜单。网络校时只从“时间设置”进入，避免两个重复入口。

交互规则：
- 短按：进入当前选中设置项；
- 长按：返回上一级。
*/
#include "apps/app_menu_base.h"
#include "sys/sys_config.h"
#include "net/net_service.h"
#include "lang/ui_strings.h"
#include "sys/sys_haptic.h"

class AppSystemSettings : public AppMenuBase {
protected:
    /**
     * 系统设置共 9 项。传感器维护功能只保留一个二级菜单入口，网络校时则由时间设置独占。
     * 这样一级菜单只表达设置类别，不直接混入具体测试页面。
     */
    int getMenuCount() override { return 9; }

    /** 返回系统设置页标题，使用已有 UIStrings 适配中英文。 */
    const char* getTitle() override {
        return UIStrings::SystemSettingsTitle(appManager.getLanguage());
    }

    /**
     * 返回滚轮菜单每一项的显示文本。
     *
     * 第 0 项 WiFi 会根据 NetService_GetState() 动态显示：
     * - 已连接：显示断开；
     * - 连接/NTP/API 中：显示网络运行中；
     * - 其他状态：显示连接无线网络。
     *
     * 其余条目由 UIStrings 按当前语言和固定语言构建配置统一生成。
     */
    const char* getItemText(int index) override {
        NetServiceState state = NetService_GetState();
        SystemLang_t lang = appManager.getLanguage();

        if (index == 0) {
            if (state == NetServiceState::SyncSuccess) return UIStrings::WifiDisconnectItem(lang);
            if (state == NetServiceState::Connecting ||
                state == NetServiceState::SyncingTime ||
                state == NetServiceState::RunningTasks) return UIStrings::WifiBusyItem(lang);
            return UIStrings::WifiConnectItem(lang);
        }

        return UIStrings::SystemSettingsItem(lang, index);
    }

    /**
     * 处理系统设置菜单短按。
     *
     * 映射关系：
     * 0 WiFi 连接/断开；
     * 1 时间设置页，网络校时只保留在该页；
     * 2 抽卡统计；
     * 3 语言切换或锁定语言提示；
     * 4 休眠；
     * 5 音量震动；
     * 6 解码动画；
     * 7 传感器校准测试二级菜单；
     * 8 返回。
     */
    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::WifiConnect);
        else if (index == 1) appManager.push(AppId::TimeSetting);
        else if (index == 2) appManager.push(AppId::GachaStats);
        else if (index == 3) {
            if (appManager.isLanguageLocked()) {
                SYS_HAPTIC_BACK();
                drawMenuUI(visual_selection);
                return;
            }
            appManager.toggleLanguage();
            drawMenuUI(visual_selection);
        }
        else if (index == 4) appManager.push(AppId::SleepSetting);
        else if (index == 5) appManager.push(AppId::VolumeSetting);
        else if (index == 6) appManager.push(AppId::AnimSetting);
        else if (index == 7) appManager.push(AppId::SensorCalibrationTest);
        else if (index == 8) appManager.popApp();
    }

    /** 长按退出系统设置页，回到上一级菜单。 */
    void onLongPressed() override { appManager.popApp(); }
};

AppSystemSettings instanceSystemSettings;
AppBase* appSystemSettings = &instanceSystemSettings;

/**
 * 传感器校准测试二级菜单。
 *
 * 本页面只负责导航，不读取传感器或复制校准逻辑：三个子页面继续各自管理动作识别上下文、
 * 人体坐标观察和地磁采样/校准生命周期。短按进入子页，长按或选择返回项回到系统设置。
 */
class AppSensorCalibrationTest : public AppMenuBase {
protected:
    int getMenuCount() override { return 4; }

    const char* getTitle() override {
        return UIStrings::SensorCalibrationTestTitle(appManager.getLanguage());
    }

    const char* getItemText(int index) override {
        return UIStrings::SensorCalibrationTestItem(appManager.getLanguage(), index);
    }

    void onItemClicked(int index) override {
        if (index == 0) appManager.push(AppId::CaduceusActionTest);
        else if (index == 1) appManager.push(AppId::HumanFrameDriftTest);
        else if (index == 2) appManager.push(AppId::MagDiagnostics);
        else if (index == 3) appManager.popApp();
    }

    void onLongPressed() override { appManager.popApp(); }
};

AppSensorCalibrationTest instanceSensorCalibrationTest;
AppBase* appSensorCalibrationTest = &instanceSensorCalibrationTest;
