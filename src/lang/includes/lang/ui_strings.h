#pragma once
#include "lang/terminal_lang.h"

// 【模块职责】集中管理短 UI 文本。
// 大段业务内容继续放在 data/zh、data/en 资源文件里；这里主要收纳菜单、按钮、提示这类高频短字符串。
namespace UIStrings
{
    // 【通用】跨应用复用的基础判断和短词。后续页面优先复用这里，避免每个 App 自己写“开启/关闭”等常用词。
    inline bool IsZh(SystemLang_t lang)
    {
        return lang == LANG_ZH;
    }

    inline const char *OnOff(SystemLang_t lang, bool enabled)
    {
        if (IsZh(lang))
            return enabled ? "开启" : "关闭";
        return enabled ? "ON" : "OFF";
    }

    inline const char *BackItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回上一级" : "BACK";
    }

    // 【主菜单】顶层应用入口名称。主菜单项数量必须和 app_main_menu.cpp 的路由数量保持一致。
    inline const char *MainMenuTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市主控菜单" : "MAIN MENU";
    }

    inline const char *MainMenuItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "接受指令",
            "纺织机",
            "定时指令",
            "但丁",
            "TT2协议",
            "专注协议",
            "硬币决定器",
            "双蛇杖",
            "提取部模拟",
            "指令档案",
            "指令推送配置",
            "使用者",
            "海",
            "业力",
            "系统高级设置",
            "进入待机模式"};
        static const char *en_items[] = {
            "RECEIVE PRESCRIPT",
            "LOOM",
            "SCHEDULES",
            "WAKEUP ALARM",
            "TT2 PROTOCOL",
            "POMODORO TIMER",
            "QUANTUM COIN",
            "CADUCEUS",
            "EXTRACTION SIM",
            "PRESCRIPT DB",
            "PUSH SETTINGS",
            "USER",
            "SEA",
            "KARMA",
            "SYSTEM SETTINGS",
            "STANDBY MODE"};

        if (index < 0 || index >= 16)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【双蛇杖】运行时标定骨架的固定文本。动作名称下标由 App 内的语义映射产生，不依赖枚举数值。
    inline const char *CaduceusTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "双蛇杖" : "CADUCEUS";
    }

    inline const char *CaduceusActionTestTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "动作测试" : "ACTION TEST";
    }

    inline const char *CaduceusResourceLoading(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在加载武器资源" : "LOADING WEAPON ASSETS";
    }

    inline const char *CaduceusResourceLoadingDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "音频与图片正在预热" : "PREPARING AUDIO AND IMAGES";
    }

    inline const char *CaduceusResourceUnavailable(SystemLang_t lang)
    {
        return IsZh(lang) ? "武器资源暂不可用" : "WEAPON ASSETS UNAVAILABLE";
    }

    inline const char *CaduceusResourceUnavailableDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "检查资源后重启重试" : "CHECK ASSETS AND RESTART";
    }

    inline const char *CaduceusWaiting(SystemLang_t lang)
    {
        return IsZh(lang) ? "等待动作..." : "WAITING FOR ACTION...";
    }

    inline const char *CaduceusCalibrationPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "请将屏幕向上平放" : "PLACE SCREEN UP";
    }

    inline const char *CaduceusCalibrationDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "保持静止，底边朝向自己" : "KEEP STILL, BOTTOM TOWARD YOU";
    }

    inline const char *CaduceusStartPrompt(SystemLang_t lang)
    {
        return IsZh(lang) ? "开始吧" : "BEGIN";
    }

    inline const char *CaduceusMissingImage(SystemLang_t lang)
    {
        return IsZh(lang) ? "武器图片缺失" : "WEAPON IMAGE MISSING";
    }

    inline const char *CaduceusActionPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "动作: " : "ACTION: ";
    }

    inline const char *CaduceusActionName(SystemLang_t lang, int index)
    {
        static const char *zh_actions[] = {"横斩", "竖斩", "斜斩 A", "斜斩 B", "突刺", "上挑"};
        static const char *en_actions[] = {
            "HORIZONTAL", "VERTICAL", "DIAGONAL A", "DIAGONAL B", "THRUST", "UPPERCUT"};
        if (index < 0 || index >= 6)
            return "";
        return IsZh(lang) ? zh_actions[index] : en_actions[index];
    }

    inline const char *CaduceusDirectionPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "横斩方向: " : "HORIZONTAL DIR: ";
    }

    inline const char *CaduceusDirectionValue(SystemLang_t lang, int direction)
    {
        if (direction == 0)
            return "--";
        if (IsZh(lang))
            return direction > 0 ? "正向" : "反向";
        return direction > 0 ? "POSITIVE" : "NEGATIVE";
    }

    inline const char *CaduceusStrengthPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "角速度峰值: " : "GYRO PEAK: ";
    }

    // 【人体坐标漂移测试】固定入口、运行状态和三页调试信息的双语文本。
    inline const char *HumanFrameDriftTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "坐标漂移测试" : "FRAME DRIFT TEST";
    }

    inline const char *HumanFrameCalibratingHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "静止约1秒 / 长按退出" : "STILL 1S / HOLD EXIT";
    }

    inline const char *HumanFrameDiscontinuous(SystemLang_t lang)
    {
        return IsZh(lang) ? "采样已断开，追踪冻结" : "SAMPLE GAP - TRACKING FROZEN";
    }

    inline const char *HumanFrameDiscontinuousDetail(SystemLang_t lang)
    {
        return IsZh(lang) ? "不能安全续接当前姿态" : "CURRENT POSE CANNOT RESUME";
    }

    inline const char *HumanFrameAttitudePage(SystemLang_t lang)
    {
        return IsZh(lang) ? "姿态" : "ATTITUDE";
    }

    inline const char *HumanFrameAccelerationPage(SystemLang_t lang)
    {
        return IsZh(lang) ? "人体坐标去重力加速度" : "HUMAN LINEAR ACCELERATION";
    }

    inline const char *HumanFrameStatisticsPage(SystemLang_t lang)
    {
        return IsZh(lang) ? "统计" : "STATS";
    }

    inline const char *HumanFrameRunningHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "旋钮换页 / 主键暂停 / 长按退出" : "TURN PAGE / CLICK PAUSE / HOLD EXIT";
    }

    inline const char *HumanFramePausedHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "显示暂停，积分继续 / 主键恢复" : "DISPLAY PAUSED, TRACKING CONTINUES";
    }

    inline const char *HumanFrameRecalibrateHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "连续短按两次侧键重新校准" : "SIDE CLICK TWICE TO RECALIBRATE";
    }

    inline const char *HumanFrameRecalibrateConfirm(SystemLang_t lang)
    {
        return IsZh(lang) ? "再按一次侧键清空并重新校准" : "SIDE CLICK AGAIN TO RESET";
    }

    // 【地磁诊断】独立磁场服务的实时数据、三维校准和操作提示。
    inline const char *MagDiagnosticsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "地磁诊断" : "MAG DIAGNOSTICS";
    }

    inline const char *MagUnavailable(SystemLang_t lang)
    {
        return IsZh(lang) ? "QMC5883P不可用，正在低频重试" : "QMC5883P OFFLINE - RETRYING";
    }

    inline const char *MagLiveHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "旋钮换页 / 串口每0.5秒输出 / 长按退出" : "TURN PAGE / SERIAL 0.5S / HOLD EXIT";
    }

    inline const char *MagCalibrationReadyHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "主键开始三维校准 / 长按退出" : "CLICK TO START 3D CAL / HOLD EXIT";
    }

    inline const char *MagCalibrationRunningHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "缓慢覆盖所有方向 / 主键结束拟合" : "ROTATE ALL DIRECTIONS / CLICK FINISH";
    }

    inline const char *MagCalibrationCancelHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "侧键取消本轮采集" : "SIDE CLICK CANCELS";
    }

    // 【业力】沉浸式木鱼页面的模式、累计次数和清空确认文本；三个模式名称暂时相同。
    inline const char *KarmaModePrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "当前模式[" : "MODE [";
    }

    inline const char *KarmaModeName(SystemLang_t lang, uint8_t mode)
    {
        (void)mode;
        return IsZh(lang) ? "业" : "KARMA";
    }

    inline const char *KarmaModeSuffix(SystemLang_t lang)
    {
        (void)lang;
        return "]";
    }

    inline const char *KarmaCountPrefix(SystemLang_t lang)
    {
        return IsZh(lang) ? "你已经累计了" : "ACCUMULATED ";
    }

    inline const char *KarmaCountUnit(SystemLang_t lang)
    {
        return IsZh(lang) ? "点" : " ";
    }

    inline const char *KarmaCountName(SystemLang_t lang)
    {
        return IsZh(lang) ? "业" : "KARMA";
    }

    inline const char *KarmaClearTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "清空业力？" : "CLEAR KARMA?";
    }

    inline const char *KarmaClearMessage(SystemLang_t lang)
    {
        return IsZh(lang) ? "仅清空当前模式" : "CURRENT MODE ONLY";
    }

    inline const char *KarmaClearHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按清空 / 长按取消" : "CLICK CLEAR / HOLD CANCEL";
    }

    inline const char *KarmaMissingImage(SystemLang_t lang)
    {
        return IsZh(lang) ? "业力图像缺失" : "KARMA IMAGE MISSING";
    }

    // 【系统设置】系统高级设置页的条目、网络状态和语言锁定提示。语言项会根据编译宏显示“切换”或“固定版本”。
    inline const char *LanguageBuildItem(SystemLang_t uiLang, SystemLang_t buildLang)
    {
        if (TerminalLang::LOCKED)
        {
            if (buildLang == LANG_ZH)
                return IsZh(uiLang) ? "语言版本: 中文" : "LANG: ZH BUILD";
            return IsZh(uiLang) ? "语言版本: 英文" : "LANG: EN BUILD";
        }
        return IsZh(uiLang) ? "切换系统语言" : "SWITCH LANGUAGE";
    }

    inline const char *LanguageLockedTip(SystemLang_t uiLang)
    {
        return IsZh(uiLang) ? "当前固件语言已固定" : "LANGUAGE LOCKED BY BUILD";
    }

    inline const char *SystemSettingsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "系统设置菜单" : "SYSTEM SETTINGS";
    }

    inline const char *WifiDisconnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "断开无线网络" : "DISCONNECT WIFI";
    }

    inline const char *WifiBusyItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络运行中..." : "WIFI BUSY...";
    }

    inline const char *WifiConnectItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "连接无线网络" : "CONNECT WIFI";
    }

    inline const char *SystemSettingsItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "",
            "同步网络时间",
            "时间设置",
            "提取部统计",
            "",
            "设定休眠时间",
            "音量与振动",
            "解码动画配置",
            "动作测试",
            "坐标漂移测试",
            "地磁数据与校准",
            "返回上一级"};
        static const char *en_items[] = {
            "",
            "SYNC NTP TIME",
            "TIME CONFIG",
            "GACHA STATS",
            "",
            "SLEEP SETTINGS",
            "VOL&HAPTIC",
            "ANIMATION SETUP",
            "ACTION TEST",
            "FRAME DRIFT TEST",
            "MAG DATA & CAL",
            "BACK TO MAIN"};

        if (index < 0 || index >= 12)
            return "";
        if (index == 4)
            return LanguageBuildItem(lang, TerminalLang::DEFAULT_LANG);
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【指令推送配置】自动推送开关、潜伏时间和进入使用者页的菜单文本。
    inline const char *PushSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送配置" : "PUSH CONFIG";
    }

    inline const char *PushEnableLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "指令推送: " : "AUTO PUSH: ";
    }

    inline const char *PushMinLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最短潜伏: " : "MIN TIME: ";
    }

    inline const char *PushMaxLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "最长潜伏: " : "MAX TIME: ";
    }

    inline const char *PushMinuteSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " 分钟" : " MIN";
    }

    inline const char *PushUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }

    inline const char *SaveAndReturnItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "保存并接入主系统" : "SAVE & RETURN";
    }

    // 【使用者】本地 ID 列表页和删除确认弹窗。ID 只用于本地显示，不对应网络账号。
    inline const char *UserTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "使用者" : "USER";
    }

    inline const char *ClearCurrentUserItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "清空当前使用者" : "CLEAR CURRENT USER";
    }

    inline const char *CurrentUserSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? " < 当前" : " < CURRENT";
    }

    inline const char *DangerTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "危险操作" : "DANGER";
    }

    inline const char *DeleteUserHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除 / 长按取消" : "CLICK DELETE / HOLD CANCEL";
    }

    // 【指令档案】档案浏览页的空状态、底部提示和删除流程文本。
    inline const char *ArchiveDeleteTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "确认删除此指令？" : "DELETE THIS RECORD?";
    }

    inline const char *ArchiveEmpty(SystemLang_t lang)
    {
        return IsZh(lang) ? "数据库为空" : "DB ARCHIVE EMPTY";
    }

    inline const char *ClickBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按返回" : "CLICK BACK";
    }

    inline const char *HoldBackHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按返回" : "HOLD BACK";
    }

    inline const char *ClickDeleteHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按删除" : "CLICK DELETE";
    }

    inline const char *HoldExitHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按退出" : "HOLD EXIT";
    }

    inline const char *PurgingRecord(SystemLang_t lang)
    {
        return IsZh(lang) ? "正在删除指令..." : "PURGING RECORD...";
    }

    // 【时间设置】时间/日期链路编辑页和周期校时菜单。月份/日期后缀也从这里分语言取值。
    inline const char **TimeManualStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定小时", "设定分钟"};
        static const char *en_items[] = {"SET HOUR", "SET MIN"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char **TimeDateStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定年份", "设定月份", "设定日期"};
        static const char *en_items[] = {"SET YEAR", "SET MONTH", "SET DAY"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *TimeEditTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "短按下一步/保存  长按返回" : "CLICK: NEXT/SAVE  LONG: BACK";
    }

    inline const char *MonthSuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? "月" : "M";
    }

    inline const char *DaySuffix(SystemLang_t lang)
    {
        return IsZh(lang) ? "日" : "D";
    }

    inline const char *TimeSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "时间设置" : "TIME CONFIG";
    }

    /** 时间设置一级菜单的动态日期时间前缀；具体 YYYY-MM-DD HH:MM 由 SysTime 提供。 */
    inline const char *CurrentTimeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "当前时间: " : "CURRENT TIME: ";
    }

    inline const char *TimeSettingItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "",
            "设置当日时间",
            "日期设置",
            "网络校时",
            "",
            ""};
        static const char *en_items[] = {
            "",
            "SET TODAY TIME",
            "SET DATE",
            "NETWORK SYNC",
            "",
            ""};

        if (index < 0 || index >= 6)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *AutoResyncLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "周期校时: " : "AUTO RESYNC: ";
    }

    inline const char *SyncPeriodLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "校时间隔: " : "SYNC PERIOD: ";
    }

    // 【解码动画配置】指令解码动画选择页。
    inline const char *AnimSettingTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "解码动画配置" : "ANIMATION SETUP";
    }

    inline const char *AnimSettingItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "默认矩阵瀑布",
            "战术游标推进",
            "逐字乱码扫描",
            "全局波浪解码"};
        static const char *en_items[] = {
            "MATRIX FALL",
            "CURSOR TYPEWRITER",
            "SEQUENTIAL GLITCH",
            "GLOBAL WAVE"};

        if (index < 0 || index >= 4)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    // 【音量与振动】左右滑条标题和震动档位。中文界面避免显示 OFF/MIN/MID/MAX。
    inline const char *VolumeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "音量输出" : "VOLUME";
    }

    inline const char *HapticLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "触感反馈" : "HAPTIC";
    }

    inline const char *LevelOff(SystemLang_t lang)
    {
        return IsZh(lang) ? "关闭" : "OFF";
    }

    inline const char *LevelMin(SystemLang_t lang)
    {
        return IsZh(lang) ? "低" : "MIN";
    }

    inline const char *LevelMid(SystemLang_t lang)
    {
        return IsZh(lang) ? "中" : "MID";
    }

    inline const char *LevelMax(SystemLang_t lang)
    {
        return IsZh(lang) ? "高" : "MAX";
    }

    // 【休眠设置】电源策略的多级菜单文本。menuLevel: 0 主菜单，1 待机，2 深度休眠。
    inline const char *SleepSettingTitle(SystemLang_t lang, int menuLevel)
    {
        if (IsZh(lang))
        {
            if (menuLevel == 0) return "电源与休眠策略";
            if (menuLevel == 1) return "待机触发 (亮屏展示)";
            if (menuLevel == 2) return "深度休眠 (物理黑屏)";
        }
        else
        {
            if (menuLevel == 0) return "POWER POLICY";
            if (menuLevel == 1) return "STANDBY (SCREEN ON)";
            if (menuLevel == 2) return "DEEP SLEEP (SCREEN OFF)";
        }
        return "";
    }

    inline const char *SleepSettingItem(SystemLang_t lang, int menuLevel, int index)
    {
        static const char *zh_main[] = {"设定待机时间", "设定深度休眠"};
        static const char *en_main[] = {"STANDBY TIMEOUT", "SLEEP TIMEOUT"};
        static const char *zh_standby[] = {"30 秒", "60 秒", "5 分钟", "永不待机"};
        static const char *en_standby[] = {"30 SEC", "60 SEC", "5 MIN", "NEVER"};
        static const char *zh_sleep[] = {"3 秒 (立刻)", "30 秒", "60 秒", "永不休眠"};
        static const char *en_sleep[] = {"3 SEC (INSTANT)", "30 SEC", "60 SEC", "NEVER"};

        if (menuLevel == 0 && index >= 0 && index < 2)
            return IsZh(lang) ? zh_main[index] : en_main[index];
        if (menuLevel == 1 && index >= 0 && index < 4)
            return IsZh(lang) ? zh_standby[index] : en_standby[index];
        if (menuLevel == 2 && index >= 0 && index < 4)
            return IsZh(lang) ? zh_sleep[index] : en_sleep[index];
        return "";
    }

    // 【闹钟】闹钟列表、小时/分钟编辑器和删除确认文本。
    inline const char **AlarmStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定小时", "设定分钟"};
        static const char *en_items[] = {"SET HR", "SET MIN"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *AlarmDeleteConfirmHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK";
    }

    inline const char *AlarmEditFirstTip(SystemLang_t lang, bool editingExisting)
    {
        if (IsZh(lang))
            return editingExisting ? "长按删此闹钟 / 单击确认" : "长按取消新建 / 单击确认";
        return editingExisting ? "LONG: DELETE / CLICK: NEXT" : "LONG: CANCEL / CLICK: NEXT";
    }

    inline const char *AlarmEditSaveTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";
    }

    inline const char *AlarmTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市唤醒闹钟" : "WAKEUP ALARM";
    }

    inline const char *AlarmAddItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 添加新闹钟" : " + ADD NEW ALARM";
    }

    inline const char *BackToMainMenuItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回主菜单" : "BACK TO MAIN MENU";
    }

    inline const char *AlarmActiveState(SystemLang_t lang, bool active)
    {
        if (IsZh(lang))
            return active ? "开" : "关";
        return active ? "ON" : "OFF";
    }

    // 【番茄钟】运行态、预设选择和当前预设编辑文本。
    inline const char *PomodoroPausedStatus(SystemLang_t lang)
    {
        return IsZh(lang) ? "已暂停 (单击继续 / 长按终止)" : "PAUSED (CLICK:RESUME/LONG:STOP)";
    }

    inline const char *PomodoroPhaseStatus(SystemLang_t lang, int phase)
    {
        if (IsZh(lang))
            return phase == 0 ? "正在执行专注..." : "正在休眠恢复...";
        return phase == 0 ? "WORKING..." : "RESTING...";
    }

    inline const char *PomodoroWorkDonePrescript(SystemLang_t lang)
    {
        return IsZh(lang) ? "专注周期结束。立刻起身活动恢复精力。" : "WORK CYCLE COMPLETED. REST IMMEDIATELY.";
    }

    inline const char *PomodoroRestDonePrescript(SystemLang_t lang)
    {
        return IsZh(lang) ? "休眠恢复完毕。系统已重置，准备接受新的专注指令。" : "REST CYCLE COMPLETED. SYSTEM RESET. READY FOR NEXT TASK.";
    }

    inline const char *PomodoroForceRestPrescript(SystemLang_t lang)
    {
        return IsZh(lang) ? "立刻去休息。" : "GO REST IMMEDIATELY.";
    }

    inline const char **PomodoroEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"专注时长", "休息时长"};
        static const char *en_items[] = {"WORK MIN", "REST MIN"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *PomodoroEditTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: OK";
    }

    inline const char *PomodoroPresetTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "选择预设配置" : "SELECT PRESET";
    }

    inline const char *PomodoroTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "番茄专注协议" : "POMODORO";
    }

    inline const char *PomodoroMenuItem(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"", "选择系统预设库", "修改当前预设时间"};
        static const char *en_items[] = {"", "SELECT PRESET", "EDIT CURRENT PRESET"};
        if (index < 0 || index >= 3)
            return "";
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *PomodoroRunLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "执行专注" : "RUN";
    }

    // 【提取部统计】抽卡统计页的标题、统计项和清空提示。
    inline const char *GachaStatsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "提取部数据库" : "EXTRACTION DATABASE";
    }

    inline const char *GachaTotalLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "总计提取" : "TOTAL PULLS";
    }

    inline const char *GachaTotalUnit(SystemLang_t lang)
    {
        return IsZh(lang) ? "次" : "";
    }

    inline const char *GachaWalpurgisLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "[W] 瓦夜总计" : "[W] WALPURGIS";
    }

    inline const char *GachaClearItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 按下清除所有记录 ]" : "[ PRESS TO CLEAR RECORDS ]";
    }

    // 【提取部模拟】十连提取页面的固定提示。人格名和结果列表来自资源池和抽取结果。
    inline const char *GachaIdleHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 单击 ] 执行十连提取" : "[ CLICK ] 10X EXTRACT";
    }

    inline const char *GachaExtractingLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "提取中..." : "EXTRACTION...";
    }

    // 【网络连接】手动 WiFi 连接页的居中状态文本。
    inline const char *WifiDisconnected(SystemLang_t lang)
    {
        return IsZh(lang) ? "已切断神经网" : "WIFI DISCONNECTED";
    }

    inline const char *WifiRunning(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络已在后台运行" : "WIFI IS RUNNING";
    }

    inline const char *WifiInit(SystemLang_t lang)
    {
        return IsZh(lang) ? "启动网络模块" : "INIT NETWORK";
    }

    inline const char *WifiConnecting(SystemLang_t lang)
    {
        return IsZh(lang) ? "连接神经网" : "CONNECTING";
    }

    inline const char *WifiConnected(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络已接入!" : "WIFI CONNECTED!";
    }

    inline const char *WifiError(SystemLang_t lang)
    {
        return IsZh(lang) ? "网络连接异常!" : "NETWORK ERROR!";
    }

    // 【网络同步】完整同步页的两行状态文本，包含连接、NTP、API、成功和失败状态。
    inline const char *NetworkSyncPrimary(SystemLang_t lang, int state)
    {
        if (state == 0)
            return IsZh(lang) ? "连接神经网" : "CONNECTING";
        if (state == 1)
            return IsZh(lang) ? "获取网络时间" : "SYNCING NTP";
        if (state == 2)
            return IsZh(lang) ? "同步成功!" : "SYNC SUCCESS!";
        if (state == 3)
            return IsZh(lang) ? "同步失败" : "SYNC FAILED";
        if (state == 4)
            return IsZh(lang) ? "获取隐秘指令" : "FETCHING API";
        return "";
    }

    inline const char *NetworkSyncSecondary(SystemLang_t lang, int state)
    {
        if (state == 0)
            return IsZh(lang) ? "请稍候" : "PLEASE WAIT";
        if (state == 1)
            return IsZh(lang) ? "NTP 同步中" : "PLEASE WAIT";
        if (state == 2)
            return IsZh(lang) ? "数据已更新" : "DATA UPDATED";
        if (state == 3)
            return IsZh(lang) ? "请检查网络" : "CHECK WIFI";
        if (state == 4)
            return IsZh(lang) ? "潜入网络中" : "DOWNLOADING";
        return "";
    }

    // 【TT2 倒计时】倒计时链路、危险确认和到点默认指令。动态数字仍由业务层填入。
    inline const char **CountdownStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定分钟", "设定秒数", "执行中"};
        static const char *en_items[] = {"SET MIN", "SET SEC", "RUNNING"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *CountdownAbortTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "确认撤收任务 ?" : "ABORT PROTOCOL ?";
    }

    inline const char *CountdownAbortHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按取消 / 单击撤收" : "LONG: CANCEL / CLICK: ABORT";
    }

    inline const char *CountdownSetTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "你想到达多久之后的未来" : "HOW FAR INTO THE FUTURE?";
    }

    inline const char *CountdownRunningTip(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按退出(后台运行) / 单击撤收" : "LONG: EXIT(BG) / CLICK: ABORT";
    }

    inline const char *CountdownDoneFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "TT2协议结束,你已经到达%d分%d秒后的未来" : "TT2 PROTOCOL ENDED. REACHED %dM %dS IN FUTURE.";
    }

    // 【纺织机】入口问句和两个操作选项。答案内容仍从 oracle_zh/en.json 资源读取。
    inline const char *OracleQuestion(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {
            "你在想什么，代行者？",
            "纺织机已经听见你的迟疑。",
            "把问题交给线与齿轮。",
            "不要说出口，都市会听见。",
            "你的想法正在被编织。",
            "答案尚未抵达，但线已经绷紧。"};
        static const char *en_items[] = {
            "What are you thinking, proxy?",
            "The loom has heard your hesitation.",
            "Give the question to thread and gear.",
            "Do not speak it. The City listens.",
            "Your thought is being woven."};
        int count = IsZh(lang) ? 6 : 5;
        if (index < 0 || index >= count)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline int OracleQuestionCount(SystemLang_t lang)
    {
        return IsZh(lang) ? 6 : 5;
    }

    inline const char *OracleOption(SystemLang_t lang, int index)
    {
        if (index == 0)
            return IsZh(lang) ? "获取纺织机回复" : "ASK THE LOOM";
        return IsZh(lang) ? "吃什么？" : "WHAT TO EAT?";
    }

    // 【硬币】量子硬币主菜单、参数设置和预设编辑器。技能名称等用户数据不放在这里。
    inline const char *CoinSettingsTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "决策参数设置" : "FLIP SETTINGS";
    }

    inline const char *CoinModeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "运行模式" : "MODE:";
    }

    inline const char *CoinSanityLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "理智波动" : "SANITY:";
    }

    inline const char *CoinCountLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "阵列数量" : "COINS:";
    }

    inline const char *CoinTypeLabel(SystemLang_t lang)
    {
        return IsZh(lang) ? "硬币型号" : "TYPE:";
    }

    inline const char *CoinModeValue(SystemLang_t lang, int mode)
    {
        if (mode == 0)
            return IsZh(lang) ? "自动" : "AUTO";
        return IsZh(lang) ? "手动" : "MANUAL";
    }

    inline const char *CoinMaterial(SystemLang_t lang, int type)
    {
        if (type == 1)
            return IsZh(lang) ? "狂气红" : "RED";
        if (type == 2)
            return IsZh(lang) ? "沉稳绿" : "GREEN";
        return IsZh(lang) ? "经典金" : "GOLD";
    }

    inline const char **CoinPresetEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"基础点", "硬币点", "抛掷数", "材质"};
        static const char *en_items[] = {"BASE", "COIN", "COUNT", "MAT"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *CoinOverrideTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "战术覆写" : "OVERRIDE";
    }

    inline const char *CoinOverrideFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "覆写 [%s]?" : "OVERRIDE [%s]?";
    }

    inline const char *CoinSaveAsNew(SystemLang_t lang)
    {
        return IsZh(lang) ? "录入为新技能?" : "SAVE AS NEW?";
    }

    inline const char *CoinPresetConfirmHint(SystemLang_t lang, bool editingExisting)
    {
        if (editingExisting)
            return IsZh(lang) ? "长按抹除 / 单击确认" : "LONG: DELETE / CLICK: CONFIRM";
        return IsZh(lang) ? "长按取消 / 单击确认" : "LONG: CANCEL / CLICK: CONFIRM";
    }

    inline const char *CoinPresetEditTip(SystemLang_t lang, bool firstNewPhase)
    {
        if (firstNewPhase)
            return IsZh(lang) ? "长按取消 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
        return IsZh(lang) ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
    }

    inline const char *CoinAutoPresetFormat(SystemLang_t lang)
    {
        return IsZh(lang) ? "预设-%d" : "PRESET-%d";
    }

    inline const char *CoinMenuTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "量子决策模块" : "QUANTUM FLIP";
    }

    inline const char *CoinQuickItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 快速基础推演 ]" : "[ QUICK ROLL ]";
    }

    inline const char *CoinNewPresetItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 新建技能预设" : " + NEW PRESET";
    }

    inline const char *CoinAdvancedItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "模块高级设定" : "ADVANCED SETTINGS";
    }

    // 【日程】日程编辑器、预设标题、过期列表和主列表。真实日程标题/正文仍属于用户数据。
    inline const char *ScheduleTitlePreset(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
        static const char *en_items[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
        if (index < 0 || index >= 4)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *ScheduleTextPreset(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"", "日程时间已到，请立即执行。"};
        static const char *en_items[] = {"", "SCHEDULE TIME REACHED. EXECUTE NOW."};
        if (index < 0 || index >= 2)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char **ScheduleEditStepNames(SystemLang_t lang)
    {
        static const char *zh_items[] = {"设定月份", "设定日期", "设定小时", "设定分钟", "选择类型", "执行动作"};
        static const char *en_items[] = {"SET MON", "SET DAY", "SET HR", "SET MIN", "SCH TYPE", "ACTION"};
        return IsZh(lang) ? zh_items : en_items;
    }

    inline const char *ScheduleTypeName(SystemLang_t lang, int index, bool compact)
    {
        static const char *zh_items[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
        static const char *en_full[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
        static const char *en_compact[] = {"ROUTINE", "MEETING", "MAINTAIN", "EMERGENCY"};
        if (index < 0 || index >= 4)
            index = 0;
        if (IsZh(lang))
            return zh_items[index];
        return compact ? en_compact[index] : en_full[index];
    }

    inline const char *ScheduleActionName(SystemLang_t lang, int index)
    {
        static const char *zh_items[] = {"随机指令", "固定提醒"};
        static const char *en_items[] = {"RANDOM", "FIXED MSG"};
        if (index < 0 || index >= 2)
            index = 0;
        return IsZh(lang) ? zh_items[index] : en_items[index];
    }

    inline const char *ScheduleDeleteConfirmHint(SystemLang_t lang)
    {
        return IsZh(lang) ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK";
    }

    inline const char *ScheduleEditTip(SystemLang_t lang, int phase, bool editingExisting, bool expired)
    {
        if (phase == 0)
        {
            if (editingExisting)
                return expired ? (IsZh(lang) ? "长按取消恢复 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT")
                               : (IsZh(lang) ? "长按删此日程 / 单击下一步" : "LONG: DELETE / CLICK: NEXT");
            return IsZh(lang) ? "长按取消新建 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
        }
        if (phase == 5)
            return IsZh(lang) ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";
        return IsZh(lang) ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
    }

    inline const char *ScheduleExpiredTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "已过期日程收容所" : "EXPIRED ARCHIVE";
    }

    inline const char *ScheduleBackToList(SystemLang_t lang)
    {
        return IsZh(lang) ? "返回上一级" : "BACK TO SCH";
    }

    inline const char *ScheduleExpiredMark(SystemLang_t lang)
    {
        return IsZh(lang) ? "(过期)" : "(EXP)";
    }

    inline const char *ScheduleTitle(SystemLang_t lang)
    {
        return IsZh(lang) ? "都市日程计划" : "SCHEDULES";
    }

    inline const char *ScheduleOpenExpiredItem(SystemLang_t lang)
    {
        return IsZh(lang) ? "[ 打开已过期日程库 ]" : "[ EXPIRED ARCHIVE ]";
    }

    inline const char *ScheduleAddItem(SystemLang_t lang)
    {
        return IsZh(lang) ? " + 登记新日程" : " + ADD SCHEDULE";
    }
}
