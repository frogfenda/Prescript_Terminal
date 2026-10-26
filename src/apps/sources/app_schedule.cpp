/*
【模块职责】日程配置与 UI。支持普通/隐藏日程添加删除、编辑器选择日期时间和过期日程列表。
【边界】到点判断、闹钟优先级、派生触发表和 RTC/ESP 下一唤醒统一由 SysCalendar 维护。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_schedule.cpp
#include "sys/app_base.h"
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "ui/ui_frame.h"
#include "sys/sys_config.h"
#include <time.h>
#include "sys/sys_psram_json.h"
#include "sys/sys_command_registry.h"
#include "sys/sys_command_result.h"
#include "sys/sys_time.h"
#include "lang/ui_strings.h"

// 当前二级菜单/编辑器操作的日程下标；新建模式下为 -1。
int g_schedule_edit_idx = -1;
// 注册 SCH 命令组；定义在文件末尾的【蓝牙命令区】。
static void Schedule_RegisterCommands();

/**
 * 日程编辑器的四种模式，决定走哪几步、保存哪些字段：
 * - Create：新建，月→日→时→分→类型→动作六步，从列表“新增”进入；
 * - Time：只改时间（月日时分四步），标题与内容原样保留，从二级菜单进入；
 * - Content：只改类型与动作两步，时间不变，从二级菜单进入；
 * - Restore：从过期库恢复，六步，保存后清除过期标记并标为已恢复。
 */
enum class ScheduleEditMode : uint8_t
{
    Create,
    Time,
    Content,
    Restore
};

namespace
{
    ScheduleEditMode s_schedule_edit_mode = ScheduleEditMode::Create;
    // 选中日程时记下的时间和标题，用来核对下标是否仍指向同一条日程（排序、到期清理和手机改动都会让数组移位）。
    uint32_t s_schedule_edit_time = 0;
    String s_schedule_edit_title;

    /**
     * 列表/过期库单击某个日程时调用：记录下标、核对信息和接下来要用的编辑模式。
     * 二级菜单进入编辑器前只需再改 s_schedule_edit_mode。
     */
    void Schedule_SelectForEdit(int index, ScheduleEditMode mode)
    {
        g_schedule_edit_idx = index;
        s_schedule_edit_mode = mode;
        s_schedule_edit_time = sysConfig.schedules[index].target_time;
        s_schedule_edit_title = sysConfig.schedules[index].title;
    }

    /**
     * 核对“当前条目”是否仍是选中时的那条日程。
     * expect_expired 为 true 用于过期库恢复，否则要求日程仍有效且未隐藏；
     * 例如二级菜单打开期间日程到点变为过期，也视为失效，调用方应退回列表且不写入。
     */
    bool Schedule_EditTargetValid(bool expect_expired)
    {
        if (g_schedule_edit_idx < 0 || g_schedule_edit_idx >= sysConfig.schedule_count)
            return false;
        const ScheduleItem &item = sysConfig.schedules[g_schedule_edit_idx];
        return item.target_time == s_schedule_edit_time &&
               item.title == s_schedule_edit_title &&
               item.is_expired == expect_expired &&
               !item.is_hidden;
    }

    const char *Get_Title_Preset(int idx)
    {
        return UIStrings::ScheduleTitlePreset(appManager.getLanguage(), idx);
    }

    const char *Get_Text_Preset(int p_idx)
    {
        return UIStrings::ScheduleTextPreset(appManager.getLanguage(), p_idx);
    }

    /**
     * 按 target_time 从早到晚排序日程数组，使菜单和到点检查都按时间顺序工作。
     * track_index 为某条日程排序前的下标，返回它排序后的新下标；不需要跟踪时传 -1（返回 -1）。
     * 改时间后二级菜单要继续指向同一条日程，靠这个返回值更新 g_schedule_edit_idx。
     */
    int Sort_Schedules(int track_index = -1)
    {
        for (int i = 0; i < sysConfig.schedule_count - 1; i++)
        {
            for (int j = 0; j < sysConfig.schedule_count - i - 1; j++)
            {
                if (sysConfig.schedules[j].target_time > sysConfig.schedules[j + 1].target_time)
                {
                    ScheduleItem temp = sysConfig.schedules[j];
                    sysConfig.schedules[j] = sysConfig.schedules[j + 1];
                    sysConfig.schedules[j + 1] = temp;
                    if (track_index == j)
                        track_index = j + 1;
                    else if (track_index == j + 1)
                        track_index = j;
                }
            }
        }
        return track_index;
    }

    // ===== 日程业务入口：本机界面和蓝牙命令共用，规则只写在这里 =====

    /**
     * 新增一条日程并按时间排序保存。
     * 规则：标题去空白后不能为空；同一时间同一标题视为重复；数量已满时先回收一条已过期日程，没有可回收的才报满。
     * 结果码：ADDED / ADDED_RECYCLED_EXPIRED（警告级）/ EXISTS（警告级）/ EMPTY_TITLE / INVALID_TIME / FULL。
     */
    SysOpResult Schedule_Add(uint32_t target_time, const String &raw_title, const String &text, bool is_hidden)
    {
        String title = raw_title;
        title.trim();
        if (title.length() == 0)
            return SysOpResult::Error("EMPTY_TITLE");
        if (target_time == 0)
            return SysOpResult::Error("INVALID_TIME");

        for (int i = 0; i < sysConfig.schedule_count; i++)
        {
            if (sysConfig.schedules[i].target_time == target_time && sysConfig.schedules[i].title == title)
                return SysOpResult::Warn("EXISTS", title);
        }

        bool recycled = false;
        if (sysConfig.schedule_count >= PrescriptConst::MAX_SCHEDULES)
        {
            for (int i = 0; i < sysConfig.schedule_count; i++)
            {
                if (!sysConfig.schedules[i].is_expired)
                    continue;
                for (int j = i; j < sysConfig.schedule_count - 1; j++)
                    sysConfig.schedules[j] = sysConfig.schedules[j + 1];
                sysConfig.schedule_count--;
                recycled = true;
                break;
            }
            if (!recycled)
                return SysOpResult::Error("FULL");
        }

        ScheduleItem &item = sysConfig.schedules[sysConfig.schedule_count++];
        item.target_time = target_time;
        item.title = title;
        item.prescript = text;
        item.is_expired = false;
        item.is_restored = false;
        item.is_hidden = is_hidden;
        Sort_Schedules();
        sysConfig.save();
        if (!is_hidden)
            SysCommand_NotifyChanged("SCH");
        return recycled ? SysOpResult::Warn("ADDED_RECYCLED_EXPIRED", title) : SysOpResult::Ok("ADDED", title);
    }

    /** 删除指定下标的日程。结果码：DELETED / NOT_FOUND。 */
    SysOpResult Schedule_DeleteAt(int index)
    {
        if (index < 0 || index >= sysConfig.schedule_count)
            return SysOpResult::Error("NOT_FOUND");
        const String title = sysConfig.schedules[index].title;
        for (int i = index; i < sysConfig.schedule_count - 1; i++)
            sysConfig.schedules[i] = sysConfig.schedules[i + 1];
        sysConfig.schedule_count--;
        sysConfig.save();
        SysCommand_NotifyChanged("SCH");
        return SysOpResult::Ok("DELETED", title);
    }

    /** 按标题删除所有同名日程（含已过期的）。结果码：DELETED / NOT_FOUND / EMPTY_TITLE。 */
    SysOpResult Schedule_DeleteByTitle(const String &raw_title)
    {
        String title = raw_title;
        title.trim();
        if (title.length() == 0)
            return SysOpResult::Error("EMPTY_TITLE");

        int deleted = 0;
        for (int i = 0; i < sysConfig.schedule_count; i++)
        {
            if (sysConfig.schedules[i].title != title)
                continue;
            for (int j = i; j < sysConfig.schedule_count - 1; j++)
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
            sysConfig.schedule_count--;
            ++deleted;
            --i;
        }
        if (deleted == 0)
            return SysOpResult::Error("NOT_FOUND", title);
        sysConfig.save();
        SysCommand_NotifyChanged("SCH");
        return SysOpResult::Ok("DELETED", title);
    }
}

/*
【页面说明】日程编辑器。步骤全集为：0 月 → 1 日 → 2 时 → 3 分 → 4 类型 → 5 动作，
按 ScheduleEditMode 只走其中一段（见枚举说明）。单击进入下一步、最后一步保存；长按回到上一步，
第一步长按直接返回上一页且不保存。删除不在这里，由二级菜单负责。
类型/动作滚轮在当前值不属于任何预设时，首项多出“保持当前”并默认选中，保存时原样保留该字段。
*/
class AppScheduleEdit : public AppBase
{
    int y, mo, d, h, m, t_idx, p_idx, phase;
    ScheduleEditMode mode;
    bool title_keep;  // 类型滚轮首项是否为“保持当前”
    bool action_keep; // 动作滚轮首项是否为“保持当前”

    DialAnimator dialAnim;       // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim; // 实例化战术链路引擎

    static constexpr int TitlePresetCount = 4;
    static constexpr int ActionPresetCount = 2;

    // 本模式的第一步与最后一步（全集下标）。
    int firstPhase() const { return mode == ScheduleEditMode::Content ? 4 : 0; }
    int lastPhase() const { return mode == ScheduleEditMode::Time ? 3 : 5; }
    int titleOptionCount() const { return TitlePresetCount + (title_keep ? 1 : 0); }
    int actionOptionCount() const { return ActionPresetCount + (action_keep ? 1 : 0); }
    bool targetExpected() const { return mode == ScheduleEditMode::Restore; }

    // 底部提示的上下文：0 新建，1 编辑已有日程，2 从过期库恢复。
    int tipContext() const
    {
        if (mode == ScheduleEditMode::Create)
            return 0;
        return mode == ScheduleEditMode::Restore ? 2 : 1;
    }

    // 【函数说明】把滚轮选择换算成要保存的标题：选中“保持当前”时返回原标题，否则返回对应预设。
    String resolveTitle(const ScheduleItem *existing) const
    {
        if (title_keep && t_idx == 0 && existing)
            return existing->title;
        return Get_Title_Preset(t_idx - (title_keep ? 1 : 0));
    }

    // 【函数说明】把滚轮选择换算成要保存的提示内容：空串表示到点时随机抽取指令。
    String resolveText(const ScheduleItem *existing) const
    {
        if (action_keep && p_idx == 0 && existing)
            return existing->prescript;
        return Get_Text_Preset(p_idx - (action_keep ? 1 : 0));
    }

    /**
     * 【函数说明】按现有标题/内容预选类型和动作滚轮，修复“编辑日程会被重置为第一个预设”的问题。
     * 标题等于某个预设就停在该预设；否则（手机写入的自定义标题）加“保持当前”首项并选中它。
     * 内容为空停在“随机指令”，等于固定提醒预设停在“固定提醒”，其他自定义文字同样加“保持当前”。
     */
    void preloadContent(const ScheduleItem &item)
    {
        title_keep = true;
        t_idx = 0;
        for (int i = 0; i < TitlePresetCount; ++i)
        {
            if (item.title == Get_Title_Preset(i))
            {
                title_keep = false;
                t_idx = i;
                break;
            }
        }

        action_keep = true;
        p_idx = 0;
        for (int i = 0; i < ActionPresetCount; ++i)
        {
            if (item.prescript == Get_Text_Preset(i))
            {
                action_keep = false;
                p_idx = i;
                break;
            }
        }
    }

    // 【函数说明】绘制日程编辑器：顶部流程链路只显示本模式的步骤，中部滚轮显示当前字段，底部提示确认或保存。
    void drawUI()
    {
        HAL_Sprite_Clear();
        SystemLang_t lang = appManager.getLanguage();

        // 1. 顶部战术链路区：只画本模式涉及的步骤；六步时按 95 留白，步骤少时放宽到 120。
        const int first = firstPhase();
        const int step_count = lastPhase() - first + 1;
        linkAnim.draw(UITheme::EditFlow::LinkY(), UIStrings::ScheduleEditStepNames(lang) + first,
                      step_count, phase - first, step_count >= 6 ? 95 : 120);

        // 中部弧面滚轮；位置与底部提示共同分配剩余高度。
        if (phase == 0)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), mo, 1, 12, "");
        else if (phase == 1)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), d, 1, SysTime_DaysInMonth(y, mo), "");
        else if (phase == 2)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), h, 0, 23, "");
        else if (phase == 3)
            dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 59, "");
        else if (phase == 4)
        {
            const char *types[TitlePresetCount + 1];
            int n = 0;
            if (title_keep)
                types[n++] = UIStrings::KeepCurrentOption(lang);
            for (int i = 0; i < TitlePresetCount; ++i)
                types[n++] = UIStrings::ScheduleTypeName(lang, i, true);
            dialAnim.drawStringDial(UITheme::EditFlow::DialY(), t_idx, types, n);
        }
        else if (phase == 5)
        {
            const char *actions[ActionPresetCount + 1];
            int n = 0;
            if (action_keep)
                actions[n++] = UIStrings::KeepCurrentOption(lang);
            for (int i = 0; i < ActionPresetCount; ++i)
                actions[n++] = UIStrings::ScheduleActionName(lang, i);
            dialAnim.drawStringDial(UITheme::EditFlow::DialY(), p_idx, actions, n);
        }

        // 4. 底部状态与操作指引
        UIFrame::DrawTip(UIStrings::ScheduleEditTip(lang, phase == first, phase == lastPhase(), tipContext()));
        HAL_Screen_Update();
    }

    /**
     * 【函数说明】把当前月日时分换算成目标时间；已经过去的日期顺延到下一年（沿用原行为）。
     * 返回 false 表示日期无效，调用方不保存。
     */
    bool computeTarget(time_t *out)
    {
        time_t now = SysTime_NowEpoch();
        if (!SysTime_LocalDateTimeToEpoch((uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)m, 0, out))
        {
            Serial.println("[日程-警告] 当前年月日时分无效，本次日程未保存。");
            return false;
        }
        if (*out < now)
        {
            ++y;
            int max_day = SysTime_DaysInMonth((uint16_t)y, (uint8_t)mo);
            if (d > max_day)
                d = max_day;
            if (!SysTime_LocalDateTimeToEpoch((uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)m, 0, out))
            {
                Serial.println("[日程-警告] 下一年度的目标日期无效，本次日程未保存。");
                return false;
            }
        }
        return true;
    }

    // 【函数说明】最后一步单击时按模式保存；失败时响长音并停留（目标失效则退回上一页）。
    void save()
    {
        if (mode != ScheduleEditMode::Create && !Schedule_EditTargetValid(targetExpected()))
        {
            Serial.println("[日程-警告] 要编辑的日程已变更，本次修改未保存。");
            SYS_SOUND_LONG();
            appManager.popApp();
            return;
        }

        time_t new_target = 0;
        if (mode != ScheduleEditMode::Content && !computeTarget(&new_target))
        {
            SYS_SOUND_LONG();
            return;
        }

        if (mode == ScheduleEditMode::Create)
        {
            // 本机新建与蓝牙 SCH.ADD 共用同一业务入口：同时间同标题去重，满了先回收过期日程。
            const SysOpResult result = Schedule_Add(static_cast<uint32_t>(new_target),
                                                    resolveTitle(nullptr), resolveText(nullptr), false);
            if (!result.ok())
            {
                Serial.printf("[日程-警告] 新增日程失败：%s。\n", result.code);
                SYS_SOUND_LONG();
                return;
            }
            appManager.popApp();
            return;
        }

        bool changed_visible = false;
        {
            ScheduleItem &item = sysConfig.schedules[g_schedule_edit_idx];
            changed_visible = !item.is_hidden;
            if (mode == ScheduleEditMode::Content || mode == ScheduleEditMode::Restore)
            {
                // 先算出新值再赋值，避免“保持当前”读到已被覆盖的字段。
                String title = resolveTitle(&item);
                String text = resolveText(&item);
                item.title = title;
                item.prescript = text;
                s_schedule_edit_title = title;
            }
            if (mode == ScheduleEditMode::Time || mode == ScheduleEditMode::Restore)
            {
                item.target_time = new_target;
                s_schedule_edit_time = (uint32_t)new_target;
            }
            if (mode == ScheduleEditMode::Restore)
            {
                item.is_expired = false;
                item.is_restored = true;
            }
            // 改时间会重新排序；跟踪新下标，让返回的二级菜单仍指向这条日程。
            g_schedule_edit_idx = Sort_Schedules(g_schedule_edit_idx);
        }
        sysConfig.save();
        if (changed_visible)
            SysCommand_NotifyChanged("SCH");
        appManager.popApp();
    }

public:
    // 【函数说明】进入日程编辑页：新建/恢复用当前日期时间作为默认值，编辑已有日程时载入原时间和内容。
    void onCreate() override
    {
        dialAnim.reset();
        mode = s_schedule_edit_mode;
        title_keep = false;
        action_keep = false;
        t_idx = 0;
        p_idx = 0;

        time_t tt = SysTime_NowEpoch();
        if (mode != ScheduleEditMode::Create)
        {
            if (!Schedule_EditTargetValid(targetExpected()))
            {
                Serial.println("[日程-警告] 要编辑的日程已变更，返回列表。");
                SYS_SOUND_LONG();
                appManager.popApp();
                return;
            }
            const ScheduleItem &item = sysConfig.schedules[g_schedule_edit_idx];
            // 过期日程恢复时默认从“现在”开始选时间，与原行为一致。
            if (mode != ScheduleEditMode::Restore)
                tt = item.target_time;
            if (mode != ScheduleEditMode::Time)
                preloadContent(item);
        }

        struct tm t_info;
        localtime_r(&tt, &t_info);
        y = t_info.tm_year + 1900;
        mo = t_info.tm_mon + 1;
        d = t_info.tm_mday;
        h = t_info.tm_hour;
        m = t_info.tm_min;

        phase = firstPhase();
        linkAnim.reset(phase - firstPhase());
        drawUI();
    }

    // 【函数说明】从子页面返回时重绘日程编辑器。
    void onResume() override { drawUI(); }

    void onLoop() override
    {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase - firstPhase());
        if (linkAnim.redrawDue(d_anim || l_anim))
            drawUI();
    }

    // 【函数说明】离开编辑器不做额外释放，编辑结果只在保存阶段写入配置。
    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 0)
        {
            mo += delta;
            if (mo < 1)
                mo = 12;
            if (mo > 12)
                mo = 1;
            int max_day = SysTime_DaysInMonth((uint16_t)y, (uint8_t)mo);
            if (d > max_day)
                d = max_day;
        }
        if (phase == 1)
        {
            int max_day = SysTime_DaysInMonth((uint16_t)y, (uint8_t)mo);
            d += delta;
            if (d < 1)
                d = max_day;
            if (d > max_day)
                d = 1;
        }
        if (phase == 2)
        {
            h += delta;
            if (h < 0)
                h = 23;
            if (h > 23)
                h = 0;
        }
        if (phase == 3)
        {
            m += delta;
            if (m < 0)
                m = 59;
            if (m > 59)
                m = 0;
        }
        if (phase == 4)
        {
            const int count = titleOptionCount();
            t_idx += delta;
            if (t_idx < 0)
                t_idx = count - 1;
            if (t_idx >= count)
                t_idx = 0;
        }
        if (phase == 5)
        {
            const int count = actionOptionCount();
            p_idx += delta;
            if (p_idx < 0)
                p_idx = count - 1;
            if (p_idx >= count)
                p_idx = 0;
        }

        dialAnim.trigger(delta);
        linkAnim.triggerDial(delta);
        SYS_SOUND_GLITCH();
        // 调值只累计动画增量，由 onLoop 在帧节拍统一绘制，避免同轮重复推屏。
    }

    // 【函数说明】短按推进到本模式的下一步；最后一步保存。
    void onKeyShort() override
    {
        dialAnim.reset();
        SYS_SOUND_CONFIRM();
        if (phase < lastPhase())
        {
            phase++;
            drawUI();
        }
        else
        {
            save();
        }
    }

    // 【函数说明】长按回到上一步；第一步长按不保存，直接返回上一页（新建/恢复即取消）。
    void onKeyLong() override
    {
        dialAnim.reset();
        SYS_SOUND_NAV();
        if (phase > firstPhase())
        {
            phase--;
            drawUI();
        }
        else
        {
            appManager.popApp();
        }
    }
};
AppScheduleEdit instanceScheduleEdit;
AppBase *appScheduleEdit = &instanceScheduleEdit;

/*
【页面说明】日程二级菜单：日程列表单击某个日程后进入。
条目：重新配置时间 / 更改标题与提示 / 删除此日程 / 返回。单击执行条目，长按返回列表。
删除与设备其他删除操作一致：选中“删除”单击后弹出确认，再次单击删除，长按取消。
每次恢复显示前核对当前条目，日程已被删除、移位或到点过期就直接退回列表。
*/
class AppScheduleItemMenu : public AppMenuBase
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
                 sysConfig.schedules[g_schedule_edit_idx].title.c_str());
        UIFrame::DrawDangerConfirm(UIStrings::DangerTitle(lang), message, UIStrings::DeleteUserHint(lang));
        HAL_Screen_Update();
        delete_redraw_tick = millis();
    }

    // 【函数说明】执行删除：再次核对目标后移除该日程并保存，然后退回日程列表。
    void deleteCurrent()
    {
        deleting = false;
        if (!Schedule_EditTargetValid(false))
        {
            Serial.println("[日程-警告] 要删除的日程已变更，本次删除未执行。");
            SYS_SOUND_LONG();
            appManager.popApp();
            return;
        }
        SYS_SOUND_GLITCH();
        Schedule_DeleteAt(g_schedule_edit_idx);
        g_schedule_edit_idx = -1;
        appManager.popApp();
    }

protected:
    int getMenuCount() override { return 4; }

    // 标题显示当前日程的标题。
    const char *getTitle() override
    {
        if (!Schedule_EditTargetValid(false))
            return "";
        return sysConfig.schedules[g_schedule_edit_idx].title.c_str();
    }

    const char *getItemText(int index) override
    {
        static String label;
        SystemLang_t lang = appManager.getLanguage();
        if (index == 0)
        {
            label = UIStrings::ScheduleItemSetTime(lang);
            if (Schedule_EditTargetValid(false))
            {
                struct tm t_info;
                time_t tt = sysConfig.schedules[g_schedule_edit_idx].target_time;
                localtime_r(&tt, &t_info);
                char date[24];
                snprintf(date, sizeof(date), " %02d/%02d %02d:%02d",
                         t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min);
                label += date;
            }
            return label.c_str();
        }
        if (index == 1)
            return UIStrings::ScheduleItemSetContent(lang);
        if (index == 2)
            return UIStrings::ScheduleItemDelete(lang);
        return UIStrings::BackItem(lang);
    }

    // 【函数说明】单击条目：改时间 / 改标题与提示 / 弹出删除确认 / 返回；确认态下单击即删除。
    void onItemClicked(int index) override
    {
        if (deleting)
        {
            deleteCurrent();
            return;
        }
        if (!Schedule_EditTargetValid(false))
        {
            appManager.popApp();
            return;
        }

        if (index == 0)
        {
            s_schedule_edit_mode = ScheduleEditMode::Time;
            appManager.push(AppId::ScheduleEdit);
        }
        else if (index == 1)
        {
            s_schedule_edit_mode = ScheduleEditMode::Content;
            appManager.push(AppId::ScheduleEdit);
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

    // 【函数说明】长按：确认态下取消删除并回到二级菜单，否则返回日程列表。
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
    // 【函数说明】每次显示前退出确认态并核对当前条目；日程已失效则直接退回列表，不再绘制本页。
    void onResume() override
    {
        deleting = false;
        if (!Schedule_EditTargetValid(false))
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
AppScheduleItemMenu instanceScheduleItemMenu;
AppBase *appScheduleItem = &instanceScheduleItemMenu;

class AppScheduleExpired : public AppMenuBase
{
    int expired_indices[PrescriptConst::MAX_SCHEDULES];
    int expired_count;

protected:
    // 【函数说明】返回菜单条目数：有效日程数量加新增、过期列表、返回等固定入口。
    int getMenuCount() override { return expired_count + 1; }
    const char *getTitle() override { return UIStrings::ScheduleExpiredTitle(appManager.getLanguage()); }
    const char *getItemText(int index) override
    {
        SystemLang_t lang = appManager.getLanguage();
        if (index == expired_count)
            return UIStrings::ScheduleBackToList(lang);
        int real_i = expired_indices[index];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        const char *mark = (index == current_selection) ? " <" : "";
        char date[32];
        snprintf(date,sizeof(date),"%02d/%02d %02d:%02d ",t_info.tm_mon+1,t_info.tm_mday,t_info.tm_hour,t_info.tm_min);
        static String label; label=date; label+=s.title; label+=" "; label+=UIStrings::ScheduleExpiredMark(lang); label+=mark;
        return label.c_str();
    }
    // 【函数说明】日程菜单点击入口：选择已有日程编辑，选择新增进入编辑器，选择过期列表进入清理页。
    void onItemClicked(int index) override
    {
        if (index < expired_count)
        {
            // 过期库交互保持原样：单击直接进入六步恢复流程。
            Schedule_SelectForEdit(expired_indices[index], ScheduleEditMode::Restore);
            appManager.push(AppId::ScheduleEdit);
        }
        else
            appManager.popApp();
    }
    // 【函数说明】日程菜单长按返回上一级。
    void onLongPressed() override { appManager.popApp(); }

public:
    void onResume() override
    {
        expired_count = 0;
        for (int i = 0; i < sysConfig.schedule_count; i++)
            // 【修改】：并且不是隐藏日程，才允许装载进 UI 列表
            if (sysConfig.schedules[i].is_expired && !sysConfig.schedules[i].is_hidden)
                expired_indices[expired_count++] = i;
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }


};
AppScheduleExpired instanceScheduleExpired;
AppBase *appScheduleExpired = &instanceScheduleExpired;

class AppScheduleMenu : public AppMenuBase
{
    int active_indices[PrescriptConst::MAX_SCHEDULES];
    int active_count;

protected:
    // 【函数说明】返回菜单条目数：有效日程数量加新增、过期列表、返回等固定入口。
    int getMenuCount() override { return active_count + 3; }
    const char *getTitle() override { return UIStrings::ScheduleTitle(appManager.getLanguage()); }
    uint16_t getItemColor(int index) override
    {
        if (index == 0 || index >= active_count + 1)
            return TFT_CYAN;
        int real_i = active_indices[index - 1];
        if (sysConfig.schedules[real_i].is_restored)
            return TFT_RED;
        return TFT_CYAN;
    }
    const char *getItemText(int index) override
    {
        SystemLang_t lang = appManager.getLanguage();
        if (index == 0)
            return UIStrings::ScheduleOpenExpiredItem(lang);
        if (index == active_count + 1)
            return UIStrings::ScheduleAddItem(lang);
        if (index == active_count + 2)
            return UIStrings::BackToMainMenuItem(lang);

        int real_i = active_indices[index - 1];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        const char *mark = (index == current_selection) ? " <" : "";
        char date[32];
        snprintf(date,sizeof(date),"%02d/%02d %02d:%02d ",t_info.tm_mon+1,t_info.tm_mday,t_info.tm_hour,t_info.tm_min);
        static String label; label=date; label+=s.title;  label+=mark;
        return label.c_str();
    }
    // 【函数说明】日程列表单击入口：已有日程进入它的二级菜单，新增进入新建编辑器，过期入口进入过期库。
    void onItemClicked(int index) override
    {
        if (index == 0)
        {
            appManager.push(AppId::ScheduleExpired);
        }
        else if (index > 0 && index <= active_count)
        {
            // 改时间、改内容、删除都在二级菜单里，列表单击只负责进入；具体模式由二级菜单再设定。
            Schedule_SelectForEdit(active_indices[index - 1], ScheduleEditMode::Time);
            appManager.push(AppId::ScheduleItem);
        }
        else if (index == active_count + 1)
        {
            g_schedule_edit_idx = -1;
            s_schedule_edit_mode = ScheduleEditMode::Create;
            appManager.push(AppId::ScheduleEdit);
        }
        else if (index == active_count + 2)
        {
            appManager.popApp();
        }
    }
    // 【函数说明】日程菜单长按返回上一级。
    void onLongPressed() override { appManager.popApp(); }

public:
    void onResume() override
    {
        active_count = 0;
        for (int i = 0; i < sysConfig.schedule_count; i++)
            if (!sysConfig.schedules[i].is_expired && !sysConfig.schedules[i].is_hidden)
                active_indices[active_count++] = i;
        if (current_selection >= getMenuCount())
            current_selection = getMenuCount() - 1;
        if (current_selection < 0)
            current_selection = 0;
        AppMenuBase::onResume();
    }
    // 【函数说明】系统启动时注册 SCH 命令组；到点业务由主循环的 SysCalendar 统一处理。
    void onSystemInit() override { Schedule_RegisterCommands(); }

};
AppScheduleMenu instanceScheduleMenu;
AppBase *appSchedule = &instanceScheduleMenu;

// ===== 【蓝牙命令区】SCH 日程 =====
// 处理函数只把参数交给日程业务入口，并把结果翻译成 ACK；隐藏日程不提供蓝牙入口。

static const SysCmdArgSpec kSchAddArgs[] = {
    SYS_ARG_INT("year", 2020, 2099),
    SYS_ARG_INT("mon", 1, 12),
    SYS_ARG_INT("day", 1, 31),
    SYS_ARG_INT("hour", 0, 23),
    SYS_ARG_INT("min", 0, 59),
    SYS_ARG_STR("title", 1, 24, ARG_TRIM),
    SYS_ARG_REST("text", 0, 0, 0),
};

static const SysCmdArgSpec kSchDelArgs[] = {
    SYS_ARG_REST("title", 1, 24, ARG_TRIM),
};

// SCH.ADD：把本地日期时间换成时间戳；2 月 30 日这类不存在的日期回 INVALID_DATE。
static void Cmd_SchAdd(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    time_t target = 0;
    if (!SysTime_LocalDateTimeToEpoch(static_cast<uint16_t>(args.num(0)), static_cast<uint8_t>(args.num(1)),
                                      static_cast<uint8_t>(args.num(2)), static_cast<uint8_t>(args.num(3)),
                                      static_cast<uint8_t>(args.num(4)), 0, &target))
    {
        ctx.error("INVALID_DATE");
        return;
    }
    ctx.ack(Schedule_Add(static_cast<uint32_t>(target), args.string(5), args.string(6), false));
}

// SCH.DEL：按标题删除所有同名日程。
static void Cmd_SchDel(SysCmdCtx &ctx, const SysCmdArgs &args)
{
    ctx.ack(Schedule_DeleteByTitle(args.string(0)));
}

// SCH.LIST：第 index 条“未过期且未隐藏”的日程 → {"dt","n","t"}。
static bool Stream_SchList(uint16_t index, JsonDocument &out)
{
    uint16_t visible = 0;
    for (int i = 0; i < sysConfig.schedule_count; ++i)
    {
        const ScheduleItem &item = sysConfig.schedules[i];
        if (item.is_expired || item.is_hidden)
            continue;
        if (visible++ != index)
            continue;
        const time_t epoch = item.target_time;
        struct tm local = {};
        localtime_r(&epoch, &local);
        char date[24];
        snprintf(date, sizeof(date), "%04d-%02d-%02dT%02d:%02d",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
        out["dt"] = date;
        out["n"] = item.title;
        out["t"] = item.prescript;
        return true;
    }
    return false;
}

static const SysCmdSpec kSchCmds[] = {
    {"LIST", nullptr, 0, CMD_SYNC, nullptr, Stream_SchList},
    {"ADD", kSchAddArgs, SYS_COUNT_OF(kSchAddArgs), 0, Cmd_SchAdd, nullptr},
    {"DEL", kSchDelArgs, SYS_COUNT_OF(kSchDelArgs), 0, Cmd_SchDel, nullptr},
};

static const SysCmdGroup kSchGroup = {"SCH", 1, SRC_ALL, kSchCmds, SYS_COUNT_OF(kSchCmds)};

static void Schedule_RegisterCommands()
{
    SysCommand_RegisterGroup(&kSchGroup);
}

// ===== 【蓝牙命令区结束】 =====
