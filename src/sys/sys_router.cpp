// 文件：src/sys/sys_router.cpp
#include "sys_router.h"
#include "sys_event.h"

#include "sys_ble.h"

#include "hal/hal.h"
#include <time.h>
#include "sys_config.h"
#include "app_manager.h"
// ==========================================
// 蓝牙消息解析与路由分发
// ==========================================
void SysRouter_ExecuteSingle(const String& msg) {
    // 1. 处理手机端拉取同步请求
   if (msg.startsWith("GET:SYNC")) {
        SysBLE_Notify("SYNC:CLEAR");
        delay(50);
        
        // 【核心解耦】：大喊一声“蓝牙要数据了！”，谁有数据谁自己往外发！
        SysEvent_Publish(EVT_BLE_SYNC_REQ, nullptr); 
        
        // 屏幕闪烁反馈 (路由器自己保留视觉效果，因为这属于网络交互)
        for (int intensity = 255; intensity >= 0; intensity -= 20) {
            uint16_t g = (intensity * 63) / 255; 
            uint16_t b = (intensity * 31) / 255; 
            uint16_t neon_cyan = (g << 5) | b;
            HAL_Draw_Rect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), neon_cyan);
            HAL_Draw_Rect(1, 1, HAL_Get_Screen_Width() - 2, HAL_Get_Screen_Height() - 2, neon_cyan);
            HAL_Screen_Update(); delay(15);
        }
        return;
    }
    // 2. 立即推送通知
    if (msg.startsWith("TXT:")) {
        String text = msg.substring(4);
        Evt_Notify_t payload = {text.c_str(), true};
        SysEvent_Publish(EVT_NOTIFY_CUSTOM, &payload); // 打包扔给邮局！
    }
    // 3. 拦截：添加闹钟
    else if (msg.startsWith("ALM:")) {
        int p1 = msg.indexOf(':', 4); 
        int p2 = msg.indexOf(':', p1 + 1); 
        int p3 = msg.indexOf(':', p2 + 1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            String name = msg.substring(p2 + 1, p3);
            String text = msg.substring(p3 + 1);
            Evt_AlmAdd_t payload = {name.c_str(), msg.substring(4, p1).toInt(), msg.substring(p1 + 1, p2).toInt(), text.c_str()};
            SysEvent_Publish(EVT_ALARM_ADD, &payload); // 扔给邮局！
        }
    } 
    // 4. 拦截：删除闹钟
    else if (msg.startsWith("ALM_DEL:")) {
        String name = msg.substring(8);
        Evt_AlmDel_t payload = {name.c_str()};
        SysEvent_Publish(EVT_ALARM_DEL, &payload); // 扔给邮局！
    } 
    // 5. 拦截：更新番茄钟
    else if (msg.startsWith("POM:")) {
        int p1 = msg.indexOf(':', 4); 
        int p2 = msg.indexOf(':', p1 + 1); 
        int p3 = msg.indexOf(':', p2 + 1);
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            String name = msg.substring(p1 + 1, p2);
            Evt_PomUpd_t payload = {msg.substring(4, p1).toInt(), name.c_str(), msg.substring(p2 + 1, p3).toInt(), msg.substring(p3 + 1).toInt()};
            SysEvent_Publish(EVT_POMODORO_UPDATE, &payload); // 扔给邮局！
        }
    } 
    // 6. 拦截：添加常规日程
    else if (msg.startsWith("SCH:")) {
        int p1 = msg.indexOf(':', 4); 
        int p2 = msg.indexOf(':', p1 + 1); 
        int p3 = msg.indexOf(':', p2 + 1); 
        int p4 = msg.indexOf(':', p3 + 1); 
        int p5 = msg.indexOf(':', p4 + 1); 
        int p6 = msg.indexOf(':', p5 + 1);
        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0 && p5 > 0 && p6 > 0) {
            time_t now; time(&now); 
            struct tm t_info; localtime_r(&now, &t_info);
            t_info.tm_year = msg.substring(4, p1).toInt() - 1900; 
            t_info.tm_mon = msg.substring(p1 + 1, p2).toInt() - 1; 
            t_info.tm_mday = msg.substring(p2 + 1, p3).toInt();
            t_info.tm_hour = msg.substring(p3 + 1, p4).toInt(); 
            t_info.tm_min = msg.substring(p4 + 1, p5).toInt(); 
            t_info.tm_sec = 0;
            
            String title = msg.substring(p5 + 1, p6);
            String text = msg.substring(p6 + 1);
            Evt_SchAdd_t payload = {(uint32_t)mktime(&t_info), title.c_str(), text.c_str(), false};
            SysEvent_Publish(EVT_SCHEDULE_ADD, &payload); // 扔给邮局！
        }
    } 
    // 7. 拦截：删除日程
    else if (msg.startsWith("SCH_DEL:")) {
        String title = msg.substring(8);
        Evt_SchDel_t payload = {title.c_str()};
        SysEvent_Publish(EVT_SCHEDULE_DEL, &payload); // 扔给邮局！
    } 
    // 8. 拦截：添加隐藏日程 (隐秘炸弹)
    else if (msg.startsWith("SCH_HID:")) {
        int p1 = msg.indexOf(':', 8); 
        int p2 = msg.indexOf(':', p1 + 1); 
        int p3 = msg.indexOf(':', p2 + 1); 
        int p4 = msg.indexOf(':', p3 + 1); 
        int p5 = msg.indexOf(':', p4 + 1); 
        int p6 = msg.indexOf(':', p5 + 1);
        if (p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0 && p5 > 0 && p6 > 0) {
            time_t now; time(&now); 
            struct tm t_info; localtime_r(&now, &t_info);
            t_info.tm_year = msg.substring(8, p1).toInt() - 1900; 
            t_info.tm_mon = msg.substring(p1 + 1, p2).toInt() - 1; 
            t_info.tm_mday = msg.substring(p2 + 1, p3).toInt();
            t_info.tm_hour = msg.substring(p3 + 1, p4).toInt(); 
            t_info.tm_min = msg.substring(p4 + 1, p5).toInt(); 
            t_info.tm_sec = 0;
            
            String title = msg.substring(p5 + 1, p6);
            String text = msg.substring(p6 + 1);
            Evt_SchAdd_t payload = {(uint32_t)mktime(&t_info), title.c_str(), text.c_str(), true};
            SysEvent_Publish(EVT_SCHEDULE_ADD, &payload); // 扔给邮局！
        }
    }
// 9. 拦截：添加新指令 (精准匹配中文)
    else if (msg.startsWith("PRE:ZH:")) {
        String text = msg.substring(7); // 跳过前缀
        // 【核心修复】：绝不写死数字，直接使用系统宏 LANG_ZH！
        Evt_PreAdd_t payload = {LANG_ZH, text.c_str()}; 
        SysEvent_Publish(EVT_PRESCRIPT_ADD, &payload);
    } 
    // 9. 拦截：添加新指令 (精准匹配英文)
    else if (msg.startsWith("PRE:EN:")) {
        String text = msg.substring(7); 
        // 【核心修复】：使用 LANG_EN
        Evt_PreAdd_t payload = {LANG_EN, text.c_str()}; 
        SysEvent_Publish(EVT_PRESCRIPT_ADD, &payload);
    }
    // 10. 拦截：删除现有指令 (中文)
    else if (msg.startsWith("PRE_DEL:ZH:")) {
        String text = msg.substring(11); 
        Evt_PreDel_t payload = {LANG_ZH, text.c_str()};
        SysEvent_Publish(EVT_PRESCRIPT_DEL, &payload);
    }
    // 10. 拦截：删除现有指令 (英文)
    else if (msg.startsWith("PRE_DEL:EN:")) {
        String text = msg.substring(11); 
        Evt_PreDel_t payload = {LANG_EN, text.c_str()};
        SysEvent_Publish(EVT_PRESCRIPT_DEL, &payload);
    }
    else if (msg.startsWith("WIFI:")) {
        int p1 = msg.indexOf(':', 5); // 寻找密码前的冒号
        if (p1 > 0) {
            String ssid = msg.substring(5, p1);
            String pass = msg.substring(p1 + 1);
            Evt_WifiSet_t payload = {ssid.c_str(), pass.c_str()};
            SysEvent_Publish(EVT_WIFI_SET, &payload); // 打包扔给邮局！
            Serial.printf("[路由中心] 收到新网段授权: SSID=%s\n", ssid.c_str());
        } 
        else if (msg.length() > 5) {
            // 如果只有 SSID，没有密码 (例如输入了 WIFI:Guest:)
            String ssid = msg.substring(5);
            if (ssid.endsWith(":")) ssid = ssid.substring(0, ssid.length() - 1);
            
            Evt_WifiSet_t payload = {ssid.c_str(), ""};
            SysEvent_Publish(EVT_WIFI_SET, &payload);
            Serial.printf("[路由中心] 收到开放网段授权: SSID=%s\n", ssid.c_str());
        }
    }
}

// ==========================================
// 【全新升级】：全自动宏指令分割器 (连发冲锋枪)
// ==========================================
void SysRouter_ProcessBLE(const String& msg) {
    int start = 0;
    int end = 0;
    
    // 循环扫描整个字符串
    while (start < msg.length()) {
        // 支持两种分隔符：换行符 \n 或者 竖线 |
        int pos1 = msg.indexOf('\n', start);
        int pos2 = msg.indexOf('|', start);
        
        // 找出最近的一个分隔符
        if (pos1 == -1) end = pos2;
        else if (pos2 == -1) end = pos1;
        else end = (pos1 < pos2) ? pos1 : pos2;

        if (end == -1) {
            end = msg.length(); // 如果后面没分隔符了，直接读到末尾
        }

        // 像切香肠一样切下一段指令
        String cmd = msg.substring(start, end);
        cmd.trim(); // 净化：砍掉首尾多余的空格或隐形回车
        
        if (cmd.length() > 0) {
            Serial.printf("[宏引擎] 提取子指令并开火: %s\n", cmd.c_str());
            
            // 把切下来的这句指令，喂给单发引擎执行！
            SysRouter_ExecuteSingle(cmd);
            
            // 【极其重要】：给系统总线 20 毫秒的喘息时间！
            // 防止你瞬间压入 5 个 UI 跳转导致 FreeRTOS 动画崩溃
            vTaskDelay(pdMS_TO_TICKS(20)); 
        }
        
        start = end + 1; // 移动指针，准备切下一段
    }
}

// ==========================================
// 网络 API 隐秘指令入口
// ==========================================
void SysRouter_ProcessAPI(uint32_t tt, const String& title, const String& text) {
    if (tt > 0) {
        // 直接打包成隐藏日程电报，下发给系统总线
        Evt_SchAdd_t payload = {tt, title.c_str(), text.c_str(), true};
        SysEvent_Publish(EVT_SCHEDULE_ADD, &payload); // 扔给邮局！
    }
}
// ==========================================
// 【新增】：NFC 物理卡片路由分发中心
// ==========================================
void _Cb_NfcScanned(void* payload) {
    Evt_NfcScanned_t* p = (Evt_NfcScanned_t*)payload;
    String text = String(p->payload);

    Serial.printf("[路由中心] 收到 NFC 物理卡片指令: %s\n", text.c_str());

    // 【核心魔法】：直接把 NFC 读到的文本，暴力喂给蓝牙的解析管线！
    // 无论是添加闹钟、日程、番茄钟还是都市指令，瞬间全部生效！
    SysRouter_ProcessBLE(text);


}

// 在系统初始化时，订阅 NFC 频道的电报
void SysRouter_Init() {
    SysEvent_Subscribe(EVT_NFC_SCANNED, _Cb_NfcScanned);
}