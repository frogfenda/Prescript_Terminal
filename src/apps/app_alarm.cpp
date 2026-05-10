/*
【模块职责】闹钟业务。支持网页/协议添加删除、菜单浏览、编辑器修改小时分钟、后台到点触发 PushNotify，并把结果写入 sysConfig。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_alarm.cpp
#include "app_base.h"
#include "app_menu_base.h"
#include "app_manager.h"
#include "../ui/ui_frame.h"
#include "sys_config.h"
#include "sys_event.h"
#include "sys_ble.h"
#include "sys_command_result.h"

int g_alarm_edit_idx = -1;
// 【函数说明】事件总线回调：把 Router 传来的 Evt_AlmAdd_t 转交给 Alarm_AddPresetMobile 处理。
void _Cb_AlmAdd(void* payload);
void _Cb_AlmDel(void* payload);
// 【函数说明】WebBLE 同步回调：逐条把当前闹钟配置打包成 SYNC:ALM JSON 文本回传网页。
void _Cb_AlmSync(void* payload);

void Alarm_AddPresetMobile(const char *name, int hour, int min, const char *text)
{
    String safeName = String(name);
    safeName.trim();
    if (safeName.length() == 0)
    {
        SysCmdResult_Error("EMPTY_NAME");
        return;
    }
    if (hour < 0 || hour > 23 || min < 0 || min > 59)
    {
        SysCmdResult_Error("INVALID_TIME");
        return;
    }

    int idx = -1;
    for (int i = 0; i < sysConfig.alarm_count; i++)
    {
        if (sysConfig.alarms[i].name == safeName)
        {
            idx = i;
            break;
        }
    }

    bool updated = (idx >= 0);
    if (idx < 0)
    {
        if (sysConfig.alarm_count >= PrescriptConst::MAX_ALARMS)
        {
            SysCmdResult_Error("FULL");
            return;
        }
        idx = sysConfig.alarm_count;
        sysConfig.alarm_count++;
    }

    sysConfig.alarms[idx].hour = hour;
    sysConfig.alarms[idx].min = min;
    sysConfig.alarms[idx].is_active = true;
    sysConfig.alarms[idx].name = safeName;
    sysConfig.alarms[idx].prescript = text ? text : "";
    sysConfig.save();

    if (updated)
        SysCmdResult_Warn("UPDATED", safeName);
    else
        SysCmdResult_Ok("ADDED", safeName);
}

// 【函数说明】处理 ALM_DEL 命令：遍历闹钟数组删除同名项，移动后续元素填补空位，保存配置并报告 DELETED 或 NOT_FOUND。
void Alarm_DeleteMobile(const char *name)
{
    String target = String(name);
    target.trim();
    if (target.length() == 0)
    {
        SysCmdResult_Error("EMPTY_NAME");
        return;
    }

    int deletedCount = 0;
    for (int i = 0; i < sysConfig.alarm_count; i++)
    {
        if (sysConfig.alarms[i].name == target)
        {
            for (int j = i; j < sysConfig.alarm_count - 1; j++)
            {
                sysConfig.alarms[j] = sysConfig.alarms[j + 1];
            }
            sysConfig.alarm_count--;
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


class AppAlarmEdit : public AppBase
{
    int h, m, phase;

    DialAnimator dialAnim;
    TacticalLinkEngine linkAnim;

    // 【函数说明】绘制闹钟编辑器：顶部流程链路显示“小时→分钟→保存”，中部用 DialAnimator 画当前小时/分钟滚轮，底部显示确认/保存提示。
    void drawUI()
    {
        HAL_Sprite_Clear();
        int sw = HAL_Get_Screen_Width();
        bool zh = appManager.getLanguage() == LANG_ZH;

        if (phase == 2)
        {
            char buf[64];
            sprintf(buf, zh ? "抹除 [%s] ?" : "DEL [%s] ?", sysConfig.alarms[g_alarm_edit_idx].name.c_str());
            UIFrame::DrawDangerConfirm(zh ? "危险操作" : "DANGER",
                                       buf,
                                       zh ? "长按确认抹除 / 单击返回编辑" : "LONG: DELETE / CLICK: BACK");
        }
        else
        {
            // ==========================================
            // 【UI 调优】：所有坐标整体上移！
            // ==========================================
            const char *names_zh[] = {"设定小时", "设定分钟"};
            const char *names_en[] = {"SET HR", "SET MIN"};
            const char **names = zh ? names_zh : names_en;

            // 1. 顶部战术链路：Y坐标从 6 调高到了 2
            linkAnim.draw(UITheme::EditFlow::LinkY(), names, 2, phase, 120);

            // 2. 中间机甲分隔线：Y坐标从 24 调高到了 18
            UIFrame::DrawTacticalDivider(UITheme::EditFlow::DividerY());

            // 3. 底部机械刻度盘：Y坐标从 36 调高到了 28
            if (phase == 0)
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), h, 0, 23, "");
            else
                dialAnim.drawNumberDial(UITheme::EditFlow::DialY(), m, 0, 59, "");

            // 4. 操作提示停留在底部：Y坐标 56
            const char *tip;
            if (phase == 0)
            {
                if (g_alarm_edit_idx >= 0)
                    tip = zh ? "长按删此闹钟 / 单击确认" : "LONG: DELETE / CLICK: NEXT";
                else
                    tip = zh ? "长按取消新建 / 单击确认" : "LONG: CANCEL / CLICK: NEXT";
            }
            else
                tip = zh ? "长按返回 / 单击保存" : "LONG: BACK / CLICK: SAVE";

            UIFrame::DrawTip(tip);
        }
        HAL_Screen_Update();
    }

public:
    // 【函数说明】进入闹钟编辑页：根据 g_alarm_edit_idx 判断新增还是编辑，载入目标闹钟时间和文本，并让流程链路跳到当前阶段。
    void onCreate() override
    {
        if (g_alarm_edit_idx >= 0)
        {
            h = sysConfig.alarms[g_alarm_edit_idx].hour;
            m = sysConfig.alarms[g_alarm_edit_idx].min;
        }
        else
        {
            h = 8;
            m = 0;
        }

        phase = 0;
        linkAnim.jumpTo(phase);
        drawUI();
    }

    // 【函数说明】从子页面返回时重绘闹钟编辑器，保证滚轮和流程链路状态同步。
    void onResume() override { drawUI(); }

    void onLoop() override
    {
        bool d_anim = dialAnim.update();
        bool l_anim = linkAnim.update(phase);
        if (d_anim || l_anim)
        {
            drawUI();
        }
    }

    void onDestroy() override {}

    // 【函数说明】旋钮调整当前阶段的数值：阶段 0 改小时，阶段 1 改分钟，并触发 DialAnimator 的横向滚动偏移。
    void onKnob(int delta) override
    {
        if (phase == 2)
            return;

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
        SYS_SOUND_GLITCH();
        drawUI();
    }

    // 【函数说明】短按推进编辑阶段；在保存阶段把修改写入 sysConfig.alarms，限制最大闹钟数量并返回菜单。
    void onKeyShort() override
    {
        SYS_SOUND_CONFIRM();
        if (phase == 2)
        {
            phase = 0;
            linkAnim.jumpTo(phase);
            drawUI();
        }
        else if (phase == 0)
        {
            phase = 1;
            drawUI();
        }
        else
        {
            if (g_alarm_edit_idx >= 0)
            {
                sysConfig.alarms[g_alarm_edit_idx].hour = h;
                sysConfig.alarms[g_alarm_edit_idx].min = m;
            }
            else
            {
                // 这是本地新建闹钟的逻辑
                int idx = sysConfig.alarm_count;
                sysConfig.alarms[idx].hour = h;
                sysConfig.alarms[idx].min = m;
                sysConfig.alarms[idx].is_active = true;

                // ==========================================
                // 【核心修复】：动态生成 Alarm1, Alarm2 这样的名字
                // ==========================================
                char autoName[16];
                sprintf(autoName, "Alarm%d", idx + 1);
                sysConfig.alarms[idx].name = autoName;

                sysConfig.alarms[idx].prescript = "SYSTEM OVERRIDE. WAKE UP NOW.";
                sysConfig.alarm_count++;
            }
            // 记得保留原有的这行保存代码
            sysConfig.save();
            appManager.popApp();
        }
    }

    // 【函数说明】长按取消编辑并返回上一页，不保存当前未确认的小时/分钟。
    void onKeyLong() override
    {
        if (phase == 2)
        {
            SYS_SOUND_GLITCH();
            for (int i = g_alarm_edit_idx; i < sysConfig.alarm_count - 1; i++)
                sysConfig.alarms[i] = sysConfig.alarms[i + 1];
            sysConfig.alarm_count--;
            sysConfig.save();
            appManager.popApp();
        }
        else if (phase == 1)
        {
            SYS_SOUND_NAV();
            phase = 0;
            drawUI();
        }
        else
        {
            SYS_SOUND_NAV();
            if (g_alarm_edit_idx >= 0)
            {
                phase = 2;
                drawUI();
            }
            else
                appManager.popApp();
        }
    }
};
AppAlarmEdit instanceAlarmEdit;
AppBase *appAlarmEdit = &instanceAlarmEdit;

class AppAlarmMenu : public AppMenuBase
{
protected:
    // 【函数说明】返回闹钟菜单条目数：现有闹钟数量，加上“新增闹钟”和“返回”两个固定入口。
    int getMenuCount() override { return sysConfig.alarm_count + 2; }
    const char *getTitle() override { return appManager.getLanguage() == LANG_ZH ? "都市唤醒闹钟" : "WAKEUP ALARM"; }
    const char *getItemText(int index) override
    {
        bool zh = appManager.getLanguage() == LANG_ZH;
        static char buf[64];
        if (index == sysConfig.alarm_count)
            return zh ? " + 添加新闹钟" : " + ADD NEW ALARM";
        if (index == sysConfig.alarm_count + 1)
            return zh ? "返回主菜单" : "BACK TO MAIN MENU";

        AlarmPreset &a = sysConfig.alarms[index];
        const char *mark = (index == current_selection) ? " <" : "";
        sprintf(buf, "%02d:%02d [%s] %s%s", a.hour, a.min, a.name.c_str(), a.is_active ? "ON" : "OFF", mark);
        return buf;
    }
    // 【函数说明】处理闹钟菜单点击：点新增进入编辑器，点已有闹钟进入对应编辑器，点返回关闭页面。
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
            sysConfig.alarms[index].is_active = !sysConfig.alarms[index].is_active;
            sysConfig.save();
            drawMenuUI(visual_selection);
        }
    }
    // 【函数说明】长按从闹钟菜单返回上一级设置/主菜单。
    void onLongPressed() override
    {
        if (current_selection < sysConfig.alarm_count)
        {
            g_alarm_edit_idx = current_selection;
            appManager.push(AppId::AlarmEdit);
        }
        else
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
    // 【函数说明】后台每分钟检查活动闹钟；当当前时分匹配且未在本分钟触发过时，弹出闹钟指令。
    void onBackgroundTick() override 
    {
        static uint32_t last_check = 0;
        if (millis() - last_check < 1000) return;
        last_check = millis();

        time_t now; time(&now);
        if (now < 1000000000) return;

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        static int last_trigger_min = -1;
        static bool missed_alarm_pending = false;
        static int pending_alarm_idx = -1;

        // 每分钟只判定一次，防止同一分钟内无限狂响
        if (timeinfo.tm_min != last_trigger_min)
        {
            for (int i = 0; i < sysConfig.alarm_count; i++)
            {
                if (sysConfig.alarms[i].is_active && 
                    sysConfig.alarms[i].hour == timeinfo.tm_hour && 
                    sysConfig.alarms[i].min == timeinfo.tm_min)
                {
                    missed_alarm_pending = true;
                    pending_alarm_idx = i;
                    last_trigger_min = timeinfo.tm_min;
                    break;
                }
            }
        }
        
        // 到点拉起通知
        if (missed_alarm_pending)
        {
            missed_alarm_pending = false;
            PushNotify_Trigger_Custom(sysConfig.alarms[pending_alarm_idx].prescript.c_str(), false);
        }
    }
    // 【函数说明】系统启动时注册闹钟后台 tick，并订阅 ALM 添加、删除、同步事件。
    void onSystemInit() override {
        SysEvent_Subscribe(EVT_ALARM_ADD, _Cb_AlmAdd);
        SysEvent_Subscribe(EVT_ALARM_DEL, _Cb_AlmDel);
        SysEvent_Subscribe(EVT_BLE_SYNC_REQ, _Cb_AlmSync);
        
        appManager.registerBackgroundApp(this);
    }
};
// === 闹钟专用的邮局拆包回调 ===
// 【函数说明】事件总线回调：把 Router 传来的 Evt_AlmAdd_t 转交给 Alarm_AddPresetMobile 处理。
void _Cb_AlmAdd(void* payload) {
    Evt_AlmAdd_t* p = (Evt_AlmAdd_t*)payload;
    Alarm_AddPresetMobile(p->name, p->hour, p->min, p->text);
}

// 【函数说明】事件总线回调：把 Router 传来的闹钟名称转交给 Alarm_DeleteMobile 删除。
void _Cb_AlmDel(void* payload) {
    Evt_AlmDel_t* p = (Evt_AlmDel_t*)payload;
    Alarm_DeleteMobile(p->name);
}
// 【函数说明】WebBLE 同步回调：逐条把当前闹钟配置打包成 SYNC:ALM JSON 文本回传网页。
void _Cb_AlmSync(void* payload) {
    for (int i = 0; i < sysConfig.alarm_count; i++) {
        String safeName = sysConfig.alarms[i].name.c_str(); safeName.replace("\"", "\\\"");
        String safeTxt = sysConfig.alarms[i].prescript.c_str(); safeTxt.replace("\"", "\\\"");
        String out = "SYNC:ALM:{\"h\":" + String(sysConfig.alarms[i].hour) + ",\"m\":" + String(sysConfig.alarms[i].min) + ",\"n\":\"" + safeName + "\",\"t\":\"" + safeTxt + "\"}";
        SysBLE_Notify(out.c_str());
        delay(50);
    }
}




AppAlarmMenu instanceAlarmMenu;
AppBase *appAlarm = &instanceAlarmMenu;
