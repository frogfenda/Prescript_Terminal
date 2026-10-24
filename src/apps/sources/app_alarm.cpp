/*
【模块职责】闹钟配置与 UI。支持网页/协议添加删除、菜单浏览和编辑器修改小时分钟，并把事实数据写入 sysConfig。
【边界】到点判断、日程优先级、派生触发表和 RTC/ESP 下一唤醒统一由 SysCalendar 维护。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_alarm.cpp
#include "sys/app_base.h"
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "ui/ui_frame.h"
#include "sys/sys_config.h"
#include "sys/sys_clock_rules.h"
#include "sys/sys_psram_json.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_command_result.h"
#include "lang/ui_strings.h"

// 当前二级菜单/编辑器操作的闹钟下标；-1 表示编辑器处于“新建”模式。
int g_alarm_edit_idx = -1;
// 注册 ALM 命令组；定义在文件末尾的【蓝牙命令区】。
static void Alarm_RegisterCommands();

namespace
{
    // 选中闹钟时记下的名字。手机按名字增删闹钟，名字在本地也唯一，用它核对下标是否仍指向同一条。
    String s_alarm_edit_name;

    // 本机新建闹钟时写入的默认提示文字（保持原有行为）。
    constexpr const char *kDefaultAlarmText = "SYSTEM OVERRIDE. WAKE UP NOW.";

    /** 同一分钟只允许一个闹钟；exclude_index 让编辑现有闹钟时忽略自身。 */
    bool Alarm_IsMinuteOccupied(int hour, int minute, int exclude_index = -1)
    {
        for (int i = 0; i < sysConfig.alarm_count; ++i)
        {
            if (i != exclude_index &&
                sysConfig.alarms[i].hour == hour &&
                sysConfig.alarms[i].min == minute)
                return true;
        }
        return false;
    }

    int Alarm_FindByName(const String &name)
    {
        for (int i = 0; i < sysConfig.alarm_count; ++i)
            if (sysConfig.alarms[i].name == name)
                return i;
        return -1;
    }

    /**
     * 列表单击某个闹钟时调用：记录下标和名字，二级菜单与编辑器共用这一份“当前条目”。
     * 输入 index 必须是有效闹钟下标；调用后 Alarm_EditTargetValid() 为真。
     */
    void Alarm_SelectForEdit(int index)
    {
        g_alarm_edit_idx = index;
        s_alarm_edit_name = sysConfig.alarms[index].name;
    }

    /**
     * 核对“当前条目”是否仍是选中时的那条闹钟。
     * 手机在二级菜单或编辑器打开期间删除/新增闹钟会让数组移位；下标越界或名字不符都视为失效，
     * 调用方应放弃写入并退回列表，避免改到另一条闹钟。
     */
    bool Alarm_EditTargetValid()
    {
        return g_alarm_edit_idx >= 0 &&
               g_alarm_edit_idx < sysConfig.alarm_count &&
               sysConfig.alarms[g_alarm_edit_idx].name == s_alarm_edit_name;
    }

    /**
     * 为本机新建的闹钟生成名字：取最小的未被占用的 AlarmN。
     * 旧做法用“当前数量+1”，删除中间项后会与现有名字重复，而手机端按名字定位闹钟。
     * 闹钟上限为 MAX_ALARMS，因此 1..MAX_ALARMS+1 之内必有空位。
     */
    String Alarm_NextAutoName()
    {
        char candidate[16];
        for (int n = 1; n <= PrescriptConst::MAX_ALARMS + 1; ++n)
        {
            snprintf(candidate, sizeof(candidate), "Alarm%d", n);
            if (Alarm_FindByName(candidate) < 0)
                break;
        }
        return String(candidate);
    }

    // ===== 闹钟业务入口：本机界面和蓝牙命令共用，规则只写在这里 =====

    /**
     * 按名称新增或更新闹钟。新增和更新后都处于开启状态。
     * 规则：名称去空白后不能为空；时间合法；同一分钟只能有一个闹钟（更新时忽略自身）；新增受数量上限限制。
     * 结果码：ADDED / UPDATED（警告级）/ EMPTY_NAME / INVALID_TIME / TIME_OCCUPIED / FULL。
     */
    SysOpResult Alarm_Upsert(const String &raw_name, int hour, int minute, const String &text)
    {
        String name = raw_name;
        name.trim();
        if (name.length() == 0)
            return SysOpResult::Error("EMPTY_NAME");
        if (!SysClockRules::AlarmTime(hour, minute))
            return SysOpResult::Error("INVALID_TIME");

        int index = Alarm_FindByName(name);
        const bool updated = index >= 0;
        if (Alarm_IsMinuteOccupied(hour, minute, index))
            return SysOpResult::Error("TIME_OCCUPIED", name);
        if (!updated)
        {
            if (sysConfig.alarm_count >= PrescriptConst::MAX_ALARMS)
                return SysOpResult::Error("FULL");
            index = sysConfig.alarm_count++;
        }

        AlarmPreset &alarm = sysConfig.alarms[index];
        alarm.hour = hour;
        alarm.min = minute;
        alarm.is_active = true;
        alarm.name = name;
        alarm.prescript = text;
        sysConfig.save();
        return updated ? SysOpResult::Warn("UPDATED", name) : SysOpResult::Ok("ADDED", name);
    }

    /** 修改指定闹钟的时间；同一分钟已有其他闹钟时拒绝。结果码：UPDATED / NOT_FOUND / INVALID_TIME / TIME_OCCUPIED。 */
    SysOpResult Alarm_SetTime(int index, int hour, int minute)
    {
        if (index < 0 || index >= sysConfig.alarm_count)
            return SysOpResult::Error("NOT_FOUND");
        if (!SysClockRules::AlarmTime(hour, minute))
            return SysOpResult::Error("INVALID_TIME");
        if (Alarm_IsMinuteOccupied(hour, minute, index))
            return SysOpResult::Error("TIME_OCCUPIED", sysConfig.alarms[index].name);
        sysConfig.alarms[index].hour = hour;
        sysConfig.alarms[index].min = minute;
        sysConfig.save();
        return SysOpResult::Ok("UPDATED", sysConfig.alarms[index].name);
    }

    /** 开启或关闭指定闹钟；开启前检查同一分钟是否已有其他闹钟。结果码：ENABLED / DISABLED / NOT_FOUND / TIME_OCCUPIED。 */
    SysOpResult Alarm_SetActive(int index, bool active)
    {
        if (index < 0 || index >= sysConfig.alarm_count)
            return SysOpResult::Error("NOT_FOUND");
        AlarmPreset &alarm = sysConfig.alarms[index];
        if (active && Alarm_IsMinuteOccupied(alarm.hour, alarm.min, index))
            return SysOpResult::Error("TIME_OCCUPIED", alarm.name);
        alarm.is_active = active;
        sysConfig.save();
        return SysOpResult::Ok(active ? "ENABLED" : "DISABLED", alarm.name);
    }

    /** 删除指定下标的闹钟。结果码：DELETED / NOT_FOUND。 */
    SysOpResult Alarm_DeleteAt(int index)
    {
        if (index < 0 || index >= sysConfig.alarm_count)
            return SysOpResult::Error("NOT_FOUND");
        const String name = sysConfig.alarms[index].name;
        for (int i = index; i < sysConfig.alarm_count - 1; i++)
            sysConfig.alarms[i] = sysConfig.alarms[i + 1];
        sysConfig.alarm_count--;
        sysConfig.save();
        return SysOpResult::Ok("DELETED", name);
    }

    /** 按名称删除闹钟（名称去空白后比较）。结果码：DELETED / NOT_FOUND / EMPTY_NAME。 */
    SysOpResult Alarm_DeleteByName(const String &raw_name)
    {
        String name = raw_name;
        name.trim();
        if (name.length() == 0)
            return SysOpResult::Error("EMPTY_NAME");
        const int index = Alarm_FindByName(name);
        if (index < 0)
            return SysOpResult::Error("NOT_FOUND", name);
        return Alarm_DeleteAt(index);
    }
}


/*
【页面说明】闹钟时间编辑器，两步：小时 → 分钟。
- g_alarm_edit_idx < 0：新建闹钟，从闹钟列表“新增”进入；第一步长按取消新建。
- g_alarm_edit_idx >= 0：只改该闹钟的时间，从二级菜单“更改时间”进入；第一步长按返回二级菜单。
删除不在这里，由二级菜单负责。
*/
class AppAlarmEdit : public AppBase
{
    int h, m, phase;

    DialAnimator dialAnim;
    TacticalLinkEngine linkAnim;

    // 【函数说明】绘制闹钟编辑器：顶部流程链路显示“小时→分钟”，中部用 DialAnimator 画当前小时/分钟滚轮，底部显示操作提示。
    void drawUI()
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();

        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::AlarmStepNames(lang), 2, phase, 120);

        // 滚轮区域由 EditFlow 按屏幕和字体行高排布。
        if (phase == 0)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), h, 0, 23, "");
        else
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 59, "");

        const char *tip = (phase == 0)
                              ? UIStrings::AlarmEditFirstTip(lang, g_alarm_edit_idx >= 0)
                              : UIStrings::AlarmEditSaveTip(lang);
        UIFrame::DrawTip(tip);
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入闹钟编辑页：根据 g_alarm_edit_idx 判断新增还是编辑；编辑时先核对目标仍有效，再载入原时间。
    void onCreate() override
    {
        dialAnim.reset();
        if (g_alarm_edit_idx >= 0)
        {
            if (!Alarm_EditTargetValid())
            {
                // 目标闹钟已被手机删除或移位：不进入编辑，直接退回（二级菜单恢复时会再退回列表）。
                Serial.println("[闹钟-警告] 要编辑的闹钟已变更，返回列表。");
                SYS_SOUND_LONG();
                appManager.popApp();
                return;
            }
            h = sysConfig.alarms[g_alarm_edit_idx].hour;
            m = sysConfig.alarms[g_alarm_edit_idx].min;
        }
        else
        {
            h = 8;
            m = 0;
        }

        phase = 0;
        linkAnim.reset(phase);
        drawUI();
    }

    // 【函数说明】从子页面返回时重绘闹钟编辑器，保证滚轮和流程链路状态同步。
    void onResume() override { drawUI(); }

    void onLoop() override
    {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (linkAnim.redrawDue(d_anim || l_anim))
        {
            drawUI();
        }
    }

    void onDestroy() override {}

    // 【函数说明】旋钮调整当前阶段的数值：阶段 0 改小时，阶段 1 改分钟，并触发 DialAnimator 的横向滚动偏移。
    void onKnob(int delta) override
    {
        if (phase == 0)
        {
            h += delta;
            if (h < 0)
                h = 23;
            if (h > 23)
                h = 0;
        }
        else
        {
            m += delta;
            if (m < 0)
                m = 59;
            if (m > 59)
                m = 0;
        }

        dialAnim.trigger(delta);
        linkAnim.triggerDial(delta);
        SYS_SOUND_GLITCH();
        // 调值只累计动画增量，由 onLoop 在帧节拍统一绘制，避免同轮重复推屏。
    }

    // 【函数说明】短按推进编辑阶段；在分钟阶段保存：编辑模式只写时间，新建模式检查容量并自动命名，保存后返回上一页。
    void onKeyShort() override
    {
        dialAnim.reset();
        SYS_SOUND_CONFIRM();
        if (phase == 0)
        {
            phase = 1;
            drawUI();
        }
        else
        {
            // 编辑期间手机可能改动了闹钟数组；目标失效时不写入，退回后由二级菜单继续退回列表。
            if (g_alarm_edit_idx >= 0 && !Alarm_EditTargetValid())
            {
                Serial.println("[闹钟-警告] 要编辑的闹钟已变更，本次修改未保存。");
                SYS_SOUND_LONG();
                appManager.popApp();
                return;
            }

            // 编辑只改时间；新建用最小未用编号自动命名，规则与蓝牙 ALM.ADD 共用同一业务入口。
            const SysOpResult result = (g_alarm_edit_idx >= 0)
                                           ? Alarm_SetTime(g_alarm_edit_idx, h, m)
                                           : Alarm_Upsert(Alarm_NextAutoName(), h, m, kDefaultAlarmText);
            if (!result.ok())
            {
                Serial.printf("[闹钟-警告] %02d:%02d 保存失败：%s。\n", h, m, result.code);
                SYS_SOUND_LONG();
                return;
            }
            appManager.popApp();
        }
    }

    // 【函数说明】长按回到上一步；第一步长按不保存，直接返回上一页（新建时即取消新建）。
    void onKeyLong() override
    {
        dialAnim.reset();
        SYS_SOUND_NAV();
        if (phase == 1)
        {
            phase = 0;
            drawUI();
        }
        else
        {
            appManager.popApp();
        }
    }
};
AppAlarmEdit instanceAlarmEdit;
AppBase *appAlarmEdit = &instanceAlarmEdit;

/*
【页面说明】闹钟二级菜单：闹钟列表单击某个闹钟后进入。
条目：状态开关 / 更改时间 / 删除此闹钟 / 返回。单击执行条目，长按返回列表。
删除与设备其他删除操作一致：选中“删除”单击后弹出确认，再次单击删除，长按取消。
每次恢复显示前核对当前条目，闹钟已被删除或移位就直接退回列表。
*/
class AppAlarmItemMenu : public AppMenuBase
{
    bool deleting = false;           // 正在显示删除确认
    uint32_t delete_redraw_tick = 0; // 确认页定时重绘，防止 HUD 刷新盖掉弹窗

    // 【函数说明】绘制删除确认弹窗，沿用设备通用的“短按删除 / 长按取消”提示。
    void drawDeleteConfirm()
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();
        char message[64];
        snprintf(message, sizeof(message), UIStrings::DeleteRecordPrompt(lang),
                 sysConfig.alarms[g_alarm_edit_idx].name.c_str());
        UIFrame::DrawDangerConfirm(UIStrings::DangerTitle(lang), message, UIStrings::DeleteUserHint(lang));
        HAL_Screen_Update();
        delete_redraw_tick = millis();
    }

    // 【函数说明】执行删除：再次核对目标后移除该闹钟并保存，然后退回闹钟列表。
    void deleteCurrent()
    {
        deleting = false;
        if (!Alarm_EditTargetValid())
        {
            Serial.println("[闹钟-警告] 要删除的闹钟已变更，本次删除未执行。");
            SYS_SOUND_LONG();
            appManager.popApp();
            return;
        }
        SYS_SOUND_GLITCH();
        Alarm_DeleteAt(g_alarm_edit_idx);
        g_alarm_edit_idx = -1;
        appManager.popApp();
    }

protected:
    int getMenuCount() override { return 4; }

    // 标题显示当前闹钟的时间和名字，例如“08:00 [Alarm1]”。
    const char *getTitle() override
    {
        static String title;
        if (!Alarm_EditTargetValid())
            return "";
        const AlarmPreset &a = sysConfig.alarms[g_alarm_edit_idx];
        char time[16];
        snprintf(time, sizeof(time), "%02u:%02u [", (unsigned)a.hour, (unsigned)a.min);
        title = time;
        title += a.name;
        title += "]";
        return title.c_str();
    }

    const char *getItemText(int index) override
    {
        static String label;
        SystemLang_t lang = appManager.getLanguage();
        if (!Alarm_EditTargetValid())
            return "";
        const AlarmPreset &a = sysConfig.alarms[g_alarm_edit_idx];
        if (index == 0)
        {
            label = UIStrings::AlarmItemStatePrefix(lang);
            label += UIStrings::AlarmActiveState(lang, a.is_active);
            return label.c_str();
        }
        if (index == 1)
        {
            char time[16];
            snprintf(time, sizeof(time), " %02u:%02u", (unsigned)a.hour, (unsigned)a.min);
            label = UIStrings::AlarmItemSetTime(lang);
            label += time;
            return label.c_str();
        }
        if (index == 2)
            return UIStrings::AlarmItemDelete(lang);
        return UIStrings::BackItem(lang);
    }

    // 【函数说明】单击条目：切换开关 / 进入时间编辑 / 弹出删除确认 / 返回；确认态下单击即删除。
    void onItemClicked(int index) override
    {
        if (deleting)
        {
            deleteCurrent();
            return;
        }
        if (!Alarm_EditTargetValid())
        {
            appManager.popApp();
            return;
        }

        if (index == 0)
        {
            // 启用前检查同一分钟是否已有其他闹钟（规则在 Alarm_SetActive 中）。
            const bool enable = !sysConfig.alarms[g_alarm_edit_idx].is_active;
            if (!Alarm_SetActive(g_alarm_edit_idx, enable).ok())
            {
                Serial.println("[闹钟-警告] 同一分钟已有其他闹钟，当前闹钟不能启用。");
                SYS_SOUND_LONG();
                return;
            }
            drawMenuUI(visual_selection);
        }
        else if (index == 1)
        {
            appManager.push(AppId::AlarmEdit);
        }
        else if (index == 2)
        {
            deleting = true;
            SYS_SOUND_GLITCH();
            drawDeleteConfirm();
        }
        else
        {
            appManager.popApp();
        }
    }

    // 【函数说明】长按：确认态下取消删除并回到二级菜单，否则返回闹钟列表。
    void onLongPressed() override
    {
        if (deleting)
        {
            deleting = false;
            SYS_SOUND_NAV();
            drawMenuUI(visual_selection);
            return;
        }
        SYS_SOUND_NAV();
        appManager.popApp();
    }

public:
    // 【函数说明】每次显示前退出确认态并核对当前条目；闹钟已失效则直接退回列表，不再绘制本页。
    void onResume() override
    {
        deleting = false;
        if (!Alarm_EditTargetValid())
        {
            appManager.popApp();
            return;
        }
        AppMenuBase::onResume();
    }

    void onKnob(int delta) override
    {
        if (deleting)
            return;
        AppMenuBase::onKnob(delta);
    }

    void onLoop() override
    {
        if (!deleting)
        {
            AppMenuBase::onLoop();
            return;
        }
        // 确认态不走菜单基类重绘，避免 HUD 或菜单动画把确认弹窗盖掉。
        if (millis() - delete_redraw_tick > 500)
            drawDeleteConfirm();
    }
};
AppAlarmItemMenu instanceAlarmItemMenu;
AppBase *appAlarmItem = &instanceAlarmItemMenu;

class AppAlarmMenu : public AppMenuBase
{
protected:
    // 【函数说明】返回闹钟菜单条目数：现有闹钟数量，加上“新增闹钟”和“返回”两个固定入口。
    int getMenuCount() override { return sysConfig.alarm_count + 2; }
    const char *getTitle() override { return UIStrings::AlarmTitle(appManager.getLanguage()); }
    const char *getItemText(int index) override
    {
        SystemLang_t lang = appManager.getLanguage();
        if (index == sysConfig.alarm_count)
            return UIStrings::AlarmAddItem(lang);
        if (index == sysConfig.alarm_count + 1)
            return UIStrings::BackToMainMenuItem(lang);

        AlarmPreset &a = sysConfig.alarms[index];
        const char *mark = (index == current_selection) ? " <" : "";
        char time[20]; snprintf(time,sizeof(time),"%02u:%02u [",(unsigned)a.hour,(unsigned)a.min);
        static String label; label=time; label+=a.name; label+="] ";
        label+=UIStrings::AlarmActiveState(lang,a.is_active); label+=mark;
        return label.c_str();
    }
    // 【函数说明】处理闹钟列表单击：点新增进入新建编辑器，点已有闹钟进入它的二级菜单，点返回关闭页面。
    void onItemClicked(int index) override
    {
        if (index == sysConfig.alarm_count)
        {
            if (sysConfig.alarm_count < 10)
            {
                g_alarm_edit_idx = -1;
                appManager.push(AppId::AlarmEdit);
            }
        }
        else if (index == sysConfig.alarm_count + 1)
        {
            appManager.popApp();
        }
        else
        {
            // 开关、改时间、删除都移到二级菜单，列表单击只负责进入。
            Alarm_SelectForEdit(index);
            appManager.push(AppId::AlarmItem);
        }
    }
    // 【函数说明】长按在列表任意位置都返回上一级，与全机“长按返回”一致。
    void onLongPressed() override
    {
        appManager.popApp();
    }

public:
    // 【函数说明】恢复闹钟菜单时重建条目显示，保证网页新增/删除后的数量立即反映到菜单。
    void onResume() override
    {
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }
    // 【函数说明】系统启动时注册 ALM 命令组；到点业务和冲突清理由 SysCalendar 负责。
    void onSystemInit() override { Alarm_RegisterCommands(); }
};
AppAlarmMenu instanceAlarmMenu;
AppBase *appAlarm = &instanceAlarmMenu;

// ===== 【蓝牙命令区】ALM 闹钟 =====
// 处理函数只把参数交给闹钟业务入口，并把结果翻译成 ACK；规则都在上面的业务入口里。

static const SysCmdArgSpec kAlmAddArgs[] = {
    SYS_ARG_INT("hour", 0, 23),
    SYS_ARG_INT("min", 0, 59),
    SYS_ARG_STR("name", 1, 24, ARG_TRIM),
    SYS_ARG_REST("text", 0, 0, 0),
};

static const SysCmdArgSpec kAlmDelArgs[] = {
    SYS_ARG_REST("name", 1, 24, ARG_TRIM),
};

// ALM.ADD：按名称新增或更新，并开启该闹钟。
static void Cmd_AlmAdd(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(Alarm_Upsert(args.string(2), args.num(0), args.num(1), args.string(3)));
}

// ALM.DEL：按名称删除。
static void Cmd_AlmDel(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(Alarm_DeleteByName(args.string(0)));
}

// ALM.LIST：第 index 个闹钟 → {"h","m","n","t","a"}。
static bool Stream_AlmList(uint16_t index, JsonDocument &out)
{
    if (index >= sysConfig.alarm_count)
        return false;
    const AlarmPreset &alarm = sysConfig.alarms[index];
    out["h"] = alarm.hour;
    out["m"] = alarm.min;
    out["n"] = alarm.name;
    out["t"] = alarm.prescript;
    out["a"] = alarm.is_active;
    return true;
}

static const SysCmdSpec kAlmCmds[] = {
    {"LIST", nullptr, 0, CMD_SYNC, nullptr, Stream_AlmList},
    {"ADD", kAlmAddArgs, SYS_COUNT_OF(kAlmAddArgs), 0, Cmd_AlmAdd, nullptr},
    {"DEL", kAlmDelArgs, SYS_COUNT_OF(kAlmDelArgs), 0, Cmd_AlmDel, nullptr},
};

static const SysCmdGroup kAlmGroup = {"ALM", 1, SRC_ALL, kAlmCmds, SYS_COUNT_OF(kAlmCmds)};

static void Alarm_RegisterCommands()
{
    SysCommand_RegisterGroup(&kAlmGroup);
}

// ===== 【蓝牙命令区结束】 =====
