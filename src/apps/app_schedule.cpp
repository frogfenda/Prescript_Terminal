/*
【模块职责】日程指令系统。支持普通/隐藏日程添加删除、编辑器选择日期时间、后台到点弹窗、过期日程列表。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_schedule.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "../ui/ui_frame.h"
#include "sys_config.h"
#include <time.h>
#include "sys_event.h"
#include "sys_ble.h"
#include "sys_command_result.h"

int g_schedule_edit_idx = -1;
// 【函数说明】事件回调：把 Router 传入的时间戳、标题、文本、隐藏标志交给 Schedule_AddMobile。
void _Cb_SchAdd(void* payload);
void _Cb_SchDel(void* payload);
void _Cb_SchSync(void* payload);
void Schedule_DeleteMobile(const char *title)
{
    String target = String(title);
    target.trim();
    if (target.length() == 0)
    {
        SysCmdResult_Error("EMPTY_TITLE");
        return;
    }

    int deletedCount = 0;
    for (int i = 0; i < sysConfig.schedule_count; i++)
    {
        if (sysConfig.schedules[i].title == target)
        {
            for (int j = i; j < sysConfig.schedule_count - 1; j++)
            {
                sysConfig.schedules[j] = sysConfig.schedules[j + 1];
            }
            sysConfig.schedule_count--;
            deletedCount++;
            i--;
        }
    }

    if (deletedCount > 0)
    {
        sysConfig.save();
        SysCmdResult_Ok("DELETED", target);
    }
    else
    {
        SysCmdResult_Error("NOT_FOUND", target);
    }
}

const char *Get_Title_Preset(int idx)
{
    const char *zh_t[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
    const char *en_t[] = {"ROUTINE", "MEETING", "MAINTENANCE", "EMERGENCY"};
    return (appManager.getLanguage() == LANG_ZH) ? zh_t[idx] : en_t[idx];
}
const char *Get_Text_Preset(int p_idx)
{
    const char *zh_p[] = {"", "日程时间已到，请立即执行。"};
    const char *en_p[] = {"", "SCHEDULE TIME REACHED. EXECUTE NOW."};
    return (appManager.getLanguage() == LANG_ZH) ? zh_p[p_idx] : en_p[p_idx];
}

// 【函数说明】按 target_time 从早到晚排序日程数组，使菜单和到点检查都按时间顺序工作。
void Sort_Schedules()
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
            }
        }
    }
}

// 【函数说明】处理 SCH/SCH_HID 命令：校验重复和容量，必要时回收过期项，再写入普通/隐藏日程并排序保存。
void Schedule_AddMobile(uint32_t target_time, const char *title, const char *text, bool is_hidden)
{
    String safeTitle = String(title);
    safeTitle.trim();
    if (safeTitle.length() == 0)
    {
        SysCmdResult_Error("EMPTY_TITLE");
        return;
    }
    if (target_time == 0)
    {
        SysCmdResult_Error("INVALID_TIME");
        return;
    }

    for (int i = 0; i < sysConfig.schedule_count; i++)
    {
        if (sysConfig.schedules[i].target_time == target_time &&
            sysConfig.schedules[i].title == safeTitle)
        {
            SysCmdResult_Warn("EXISTS", safeTitle);
            return;
        }
    }

    bool recycledExpired = false;
    if (sysConfig.schedule_count >= PrescriptConst::MAX_SCHEDULES)
    {
        bool freed = false;
        for (int i = 0; i < sysConfig.schedule_count; i++)
        {
            if (sysConfig.schedules[i].is_expired)
            {
                for (int j = i; j < sysConfig.schedule_count - 1; j++)
                    sysConfig.schedules[j] = sysConfig.schedules[j + 1];
                sysConfig.schedule_count--;
                freed = true;
                recycledExpired = true;
                break;
            }
        }
        if (!freed)
        {
            SysCmdResult_Error("FULL");
            return;
        }
    }

    int idx = sysConfig.schedule_count;
    sysConfig.schedules[idx].target_time = target_time;
    sysConfig.schedules[idx].title = safeTitle;
    sysConfig.schedules[idx].prescript = text ? text : "";
    sysConfig.schedules[idx].is_expired = false;
    sysConfig.schedules[idx].is_restored = false;
    sysConfig.schedules[idx].is_hidden = is_hidden;
    sysConfig.schedule_count++;
    Sort_Schedules();
    sysConfig.save();

    if (recycledExpired)
        SysCmdResult_Warn("ADDED_RECYCLED_EXPIRED", safeTitle);
    else
        SysCmdResult_Ok("ADDED", safeTitle);
}

class AppScheduleEdit : public AppBase
{
    int mo, d, h, m, t_idx, p_idx, phase;

    DialAnimator dialAnim;       // 实例化刻度盘引擎
    TacticalLinkEngine linkAnim; // 实例化战术链路引擎

    // 【函数说明】绘制日程编辑器：顶部流程链路显示月/日/时/分/类型，中部滚轮显示当前字段，底部提示确认或保存。
    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 6)
        {
            char buf[64];
            sprintf(buf, zh ? "抹除 [%s]?" : "DEL [%s]?", sysConfig.schedules[g_schedule_edit_idx].title.c_str());
            UIFrame::DrawDangerConfirm(zh ? "危险操作" : "DANGER",
                                       buf,
                                       zh ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK");
        }
        else
        {
            // 1. 顶部战术链路区
            const char *names_zh[] = {"设定月份", "设定日期", "设定小时", "设定分钟", "选择类型", "执行动作"};
            const char *names_en[] = {"SET MON", "SET DAY", "SET HR", "SET MIN", "SCH TYPE", "ACTION"};
            const char **names = zh ? names_zh : names_en;

            // 节点较多，间距设为 95
            linkAnim.draw(UITheme::EditFlow::LinkY(), names, 6, phase, 95);

            // 2. 中间机甲分隔线
            UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

            // 3. 底部动态机械刻度盘
            if (phase == 0)
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), mo, 1, 12, "");
            else if (phase == 1)
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), d, 1, 31, "");
            else if (phase == 2)
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), h, 0, 23, "");
            else if (phase == 3)
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 59, "");
            else if (phase == 4)
            {
                const char *t_zh[] = {"常规待办", "高维会议", "系统维护", "突发任务"};
                const char *t_en[] = {"ROUTINE", "MEETING", "MAINTAIN", "EMERGENCY"};
                dialAnim.drawStringDial(UITheme::EditFlow::DialY(), t_idx, zh ? t_zh : t_en, 4);
            }
            else if (phase == 5)
            {
                const char *p_zh[] = {"随机指令", "固定提醒"};
                const char *p_en[] = {"RANDOM", "FIXED MSG"};
                dialAnim.drawStringDial(UITheme::EditFlow::DialY(), p_idx, zh ? p_zh : p_en, 2);
            }

            // 4. 底部状态与操作指引
            const char *tip = zh ? "长按返回 / 单击下一步" : "LONG: BACK / CLICK: NEXT";
            if (phase == 0)
            {
                if (g_schedule_edit_idx >= 0)
                {
                    if (!sysConfig.schedules[g_schedule_edit_idx].is_expired)
                        tip = zh ? "长按删此日程 / 单击下一步" : "LONG: DELETE / CLICK: NEXT";
                    else
                        tip = zh ? "长按取消恢复 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
                }
                else
                    tip = zh ? "长按取消新建 / 单击下一步" : "LONG: CANCEL / CLICK: NEXT";
            }
            else if (phase == 5)
                tip = zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";

            UIFrame::DrawTip(tip);
        }
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入日程编辑页：新增时用当前日期时间作为默认值，编辑时载入已有日程时间和类型。
    void onCreate() override
    {
        time_t now;
        time(&now);
        struct tm t_info;
        localtime_r(&now, &t_info);
        if (g_schedule_edit_idx >= 0)
        {
            time_t tt = sysConfig.schedules[g_schedule_edit_idx].target_time;
            if (sysConfig.schedules[g_schedule_edit_idx].is_expired)
                tt = now;
            struct tm s_info;
            localtime_r(&tt, &s_info);
            mo = s_info.tm_mon + 1;
            d = s_info.tm_mday;
            h = s_info.tm_hour;
            m = s_info.tm_min;
            t_idx = 0;
            p_idx = 0;
        }
        else
        {
            mo = t_info.tm_mon + 1;
            d = t_info.tm_mday;
            h = t_info.tm_hour;
            m = t_info.tm_min;
            t_idx = 0;
            p_idx = 0;
        }
        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }

    // 【函数说明】从子页面返回时重绘日程编辑器。
    void onResume() override { drawUI(); }

    void onLoop() override
    {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (d_anim || l_anim)
            drawUI();
    }

    // 【函数说明】离开编辑器不做额外释放，编辑结果只在保存阶段写入配置。
    void onDestroy() override {}

    void onKnob(int delta) override
    {
        if (phase == 6)
            return;
        if (phase == 0)
        {
            mo += delta;
            if (mo < 1)
                mo = 12;
            if (mo > 12)
                mo = 1;
        }
        if (phase == 1)
        {
            d += delta;
            if (d < 1)
                d = 31;
            if (d > 31)
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
            t_idx += delta;
            if (t_idx < 0)
                t_idx = 3;
            if (t_idx > 3)
                t_idx = 0;
        }
        if (phase == 5)
        {
            p_idx += delta;
            if (p_idx < 0)
                p_idx = 1;
            if (p_idx > 1)
                p_idx = 0;
        }

        dialAnim.trigger(delta);
        SYS_SOUND_GLITCH();
        drawUI();
    }

    // 【函数说明】短按推进编辑阶段；最后保存 timestamp、标题、文本和隐藏标志到日程数组。
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        if (phase == 6)
        {
            phase = 0;
            linkAnim.jumpTo(phase);
            drawUI();
        }
        else if (phase < 5)
        {
            phase++;
            drawUI();
        }
        else
        {
            time_t now;
            time(&now);
            struct tm t_info;
            localtime_r(&now, &t_info);
            t_info.tm_mon = mo - 1;
            t_info.tm_mday = d;
            t_info.tm_hour = h;
            t_info.tm_min = m;
            t_info.tm_sec = 0;
            time_t new_target = mktime(&t_info);
            if (new_target < now)
            {
                t_info.tm_year += 1;
                new_target = mktime(&t_info);
            }

            if (g_schedule_edit_idx >= 0)
            {
                sysConfig.schedules[g_schedule_edit_idx].target_time = new_target;
                sysConfig.schedules[g_schedule_edit_idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[g_schedule_edit_idx].prescript = Get_Text_Preset(p_idx);
                if (sysConfig.schedules[g_schedule_edit_idx].is_expired)
                {
                    sysConfig.schedules[g_schedule_edit_idx].is_expired = false;
                    sysConfig.schedules[g_schedule_edit_idx].is_restored = true;
                }
            }
            else
            {
                int idx = sysConfig.schedule_count;
                sysConfig.schedules[idx].target_time = new_target;
                sysConfig.schedules[idx].title = Get_Title_Preset(t_idx);
                sysConfig.schedules[idx].prescript = Get_Text_Preset(p_idx);
                sysConfig.schedules[idx].is_expired = false;
                sysConfig.schedules[idx].is_restored = false;
                sysConfig.schedules[idx].is_hidden = false; // 【新增】：本机手工新建绝对不隐藏
                sysConfig.schedule_count++;
            }
            Sort_Schedules();
            sysConfig.save();
            appManager.popApp();
        }
    }

    // 【函数说明】长按取消编辑并返回上一级。
    void onKeyLong() override
    {
        if (phase == 6)
        {
            SYS_SOUND_GLITCH();
            for (int i = g_schedule_edit_idx; i < sysConfig.schedule_count - 1; i++)
                sysConfig.schedules[i] = sysConfig.schedules[i + 1];
            sysConfig.schedule_count--;
            sysConfig.save();
            appManager.popApp();
        }
        else if (phase > 0)
        {
            SYS_SOUND_NAV();
            phase--;
            drawUI();
        }
        else
        {
            SYS_SOUND_NAV();
            if (g_schedule_edit_idx >= 0 && !sysConfig.schedules[g_schedule_edit_idx].is_expired)
            {
                phase = 6;
                drawUI();
            }
            else
            {
                appManager.popApp();
            }
        }
    }
};
AppScheduleEdit instanceScheduleEdit;
AppBase *appScheduleEdit = &instanceScheduleEdit;

class AppScheduleExpired : public AppMenuBase
{
    int expired_indices[15];
    int expired_count;

protected:
    // 【函数说明】返回菜单条目数：有效日程数量加新增、过期列表、返回等固定入口。
    int getMenuCount() override { return expired_count + 1; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "已过期日程收容所" : "EXPIRED ARCHIVE"; }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == expired_count)
            return zh ? "返回上一级" : "BACK TO SCH";
        int real_i = expired_indices[index];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        static char buf[64];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s %s%s", t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), zh ? "(过期)" : "(EXP)", mark);
        return buf;
    }
    // 【函数说明】日程菜单点击入口：选择已有日程编辑，选择新增进入编辑器，选择过期列表进入清理页。
    void onItemClicked(int index) override
    {
        if (index < expired_count)
        {
            g_schedule_edit_idx = expired_indices[index];
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
    int active_indices[15];
    int active_count;

protected:
    // 【函数说明】返回菜单条目数：有效日程数量加新增、过期列表、返回等固定入口。
    int getMenuCount() override { return active_count + 3; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "都市日程计划" : "SCHEDULES"; }
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
        bool zh = appManager.getLanguage() == LANG_ZH;
        if (index == 0)
            return zh ? "[ 打开已过期日程库 ]" : "[ EXPIRED ARCHIVE ]";
        if (index == active_count + 1)
            return zh ? " + 登记新日程" : " + ADD SCHEDULE";
        if (index == active_count + 2)
            return zh ? "返回主菜单" : "BACK TO MAIN MENU";

        int real_i = active_indices[index - 1];
        ScheduleItem &s = sysConfig.schedules[real_i];
        struct tm t_info;
        time_t tt = s.target_time;
        localtime_r(&tt, &t_info);
        static char buf[64];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d/%02d %02d:%02d %s%s", t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min, s.title.c_str(), mark);
        return buf;
    }
    // 【函数说明】日程菜单点击入口：选择已有日程编辑，选择新增进入编辑器，选择过期列表进入清理页。
    void onItemClicked(int index) override
    {
        if (index == 0)
        {
            appManager.push(AppId::ScheduleExpired);
        }
        else if (index > 0 && index <= active_count)
        {
            g_schedule_edit_idx = active_indices[index - 1];
            appManager.push(AppId::ScheduleEdit);
        }
        else if (index == active_count + 1)
        {
            g_schedule_edit_idx = -1;
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
    // 【函数说明】订阅日程添加/删除/同步事件并注册后台 tick，让日程到点能在任意页面触发。
    void onSystemInit() override {
        // 1. 去邮局订阅自己的频道
        SysEvent_Subscribe(EVT_SCHEDULE_ADD, _Cb_SchAdd);
        SysEvent_Subscribe(EVT_SCHEDULE_DEL, _Cb_SchDel);
        SysEvent_Subscribe(EVT_BLE_SYNC_REQ, _Cb_SchSync);
        
        // 2. 向系统总管申请后台巡逻权限！
        appManager.registerBackgroundApp(this);
    }
       void onBackgroundTick() override
    {
        static uint32_t last_check = 0;
        if (millis() - last_check < 1000)
            return; // 1秒检查一次
        last_check = millis();

        time_t now;
        time(&now);
        if (now < 1000000000)
            return; // 时间没对准时不查

        bool need_save = false;
        for (int i = 0; i < sysConfig.schedule_count; i++)
        {
            ScheduleItem &s = sysConfig.schedules[i];

            // ==========================================
            // 【核心修复 3：隐秘炸弹阅后即焚！】
            // ==========================================
            bool should_destroy = false;
            if (s.is_expired) {
                if (s.is_hidden) {
                    // 隐秘日程：一旦引爆变成 expired，下一秒立刻彻底粉碎！不占内存！
                    should_destroy = true; 
                } else if (now - s.expire_time > 86400) {
                    // 常规日程：保留 24 小时在“收容所”供查看
                    should_destroy = true; 
                }
            }

            if (should_destroy)
            {
                for (int j = i; j < sysConfig.schedule_count - 1; j++)
                    sysConfig.schedules[j] = sysConfig.schedules[j + 1];
                sysConfig.schedule_count--;
                need_save = true;
                i--; // 游标回退
                continue;
            }

            // 处理到点引爆
            if (!s.is_expired && now >= s.target_time)
            {
                s.is_expired = true;
                s.expire_time = now;
                s.is_restored = false;
                need_save = true;

                if (s.prescript.length() == 0)
                    PushNotify_Trigger_Random(false);
                else
                    PushNotify_Trigger_Custom(s.prescript.c_str(), false);
                
                // 💡 妙笔：引爆后 is_expired 变为 true。
                // 到了下一秒的循环，隐秘日程就会被上面的 should_destroy 瞬间碾碎！
            }
        }
        if (need_save)
            sysConfig.save();
    }

};
// === 日程表专用的邮局拆包回调 ===
// 【函数说明】事件回调：把 Router 传入的时间戳、标题、文本、隐藏标志交给 Schedule_AddMobile。
void _Cb_SchAdd(void *payload)
{
    Evt_SchAdd_t *p = (Evt_SchAdd_t *)payload;
    Schedule_AddMobile(p->tt, p->title, p->text, p->is_hidden);
}

// 【函数说明】事件回调：把 Router 传入的标题交给 Schedule_DeleteMobile。
void _Cb_SchDel(void *payload)
{
    Evt_SchDel_t *p = (Evt_SchDel_t *)payload;
    Schedule_DeleteMobile(p->title);
}

// 收到路由器的同步口哨声，日程表自己把自己的数据发给蓝牙！
// 【函数说明】WebBLE 同步回调：把日程数组打包成 SYNC:SCH JSON 回传网页。
void _Cb_SchSync(void *payload)
{
    for (int i = 0; i < sysConfig.schedule_count; i++)
    {
        if (sysConfig.schedules[i].is_expired || sysConfig.schedules[i].is_hidden)
            continue;
        struct tm t_info;
        time_t tt = sysConfig.schedules[i].target_time;
        localtime_r(&tt, &t_info);
        char dt[32];
        sprintf(dt, "%04d-%02d-%02dT%02d:%02d", t_info.tm_year + 1900, t_info.tm_mon + 1, t_info.tm_mday, t_info.tm_hour, t_info.tm_min);
        String safeName = sysConfig.schedules[i].title.c_str();
        safeName.replace("\"", "\\\"");
        String safeTxt = sysConfig.schedules[i].prescript.c_str();
        safeTxt.replace("\"", "\\\"");
        String out = "SYNC:SCH:{\"dt\":\"" + String(dt) + "\",\"n\":\"" + safeName + "\",\"t\":\"" + safeTxt + "\"}";
        SysBLE_Notify(out.c_str());
        delay(50);
    }
}



AppScheduleMenu instanceScheduleMenu;
AppBase *appSchedule = &instanceScheduleMenu;
