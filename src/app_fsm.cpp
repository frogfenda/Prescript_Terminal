#include "app_fsm.h"
#include "hal.h"
#include "prescript_logic.h"
#include <Arduino.h>

typedef enum {
    STATE_SLEEP = 0,
    STATE_STANDBY,
    STATE_MENU,          // 【全新加入】：独立的菜单状态
    STATE_SHOWING_TEXT
} SystemState;

static SystemState current_state = STATE_STANDBY;
static uint32_t text_show_timer = 0;
static uint32_t idle_timer = 0;
static uint32_t last_tick = 0;

// === 中文菜单配置 ===
const char* menu_items[] = {
    "执行都市指令",
    "网络时间同步",
    "系统休眠设定",
    "返回待机画面"
};
const int menu_count = 4;
static int current_selection = 0;

// === 渲染菜单界面 ===
void Render_Menu_UI(int selection) {
    Screen_Clear();
    Screen_DrawHeader(); // 顶部画上红色的 [ PRESCRIPT ]
    
    int start_y = 60;    // 第一行菜单的 Y 坐标
    int spacing = 35;    // 每行菜单的间距

    for (int i = 0; i < menu_count; i++) {
        char buf[64];
        if (i == selection) {
            // 被选中的项目：加一个箭头符号，并且向右缩进 10 个像素突出显示！
            sprintf(buf, "> %s", menu_items[i]);
            Screen_ShowChineseLine(30, start_y + i * spacing, buf);
        } else {
            // 没被选中的项目：正常左对齐
            sprintf(buf, "  %s", menu_items[i]);
            Screen_ShowChineseLine(20, start_y + i * spacing, buf);
        }
    }
    Screen_Update(); // 一次性推送到屏幕，防止闪烁
}

void App_FSM_Init(void) {
    HAL_Init();
    randomSeed(analogRead(A0)); 
    current_state = STATE_STANDBY;
    last_tick = millis();

    Screen_Clear();
    Screen_DrawStandbyImage(); 
}

void App_FSM_Run(void) {
    uint32_t current_time = millis();
    uint32_t delta_time = current_time - last_tick;
    last_tick = current_time;

    // ==========================================
    // 1. 处理旋钮滚动事件 (只在菜单界面有效)
    // ==========================================
    int knob_delta = Get_Knob_Delta();
    if (current_state == STATE_MENU && knob_delta != 0) {
        current_selection += knob_delta;
        
        // 保证光标在 0 到 3 之间循环滚动
        if (current_selection < 0) current_selection = menu_count - 1;
        if (current_selection >= menu_count) current_selection = 0;
        
        Render_Menu_UI(current_selection);
        Buzzer_Random_Glitch(); // 转动时发出极短的赛博电流杂音
        idle_timer = 0;         // 只要人在操作，就不准休眠
    }

    // ==========================================
    // 2. 处理按键按下事件
    // ==========================================
    if (Is_Key_Pressed()) {
        delay(20); // 物理按键防抖
        if (Is_Key_Pressed()) {
            while(Is_Key_Pressed()) { delay(10); yield(); } // 等待手松开
            
            idle_timer = 0;

            if (current_state == STATE_SLEEP) {
                // 唤醒到待机
                current_state = STATE_STANDBY;
                Screen_Clear();
                Screen_DrawStandbyImage();
            } 
            else if (current_state == STATE_STANDBY) {
                // 待机状态下按下，唤出菜单！
                current_state = STATE_MENU;
                current_selection = 0; // 默认选中第一项
                Render_Menu_UI(current_selection);
                Buzzer_Play_Tone(2000, 40); // 唤出菜单的提示音
            }
            else if (current_state == STATE_MENU) {
                // 菜单状态下按下：执行选中的功能！
                Buzzer_Play_Tone(2500, 80); 
                
                if (current_selection == 0) {
                    // 执行原本的狂暴乱码解密！
                    Prescript_Action();
                    current_state = STATE_SHOWING_TEXT;
                    text_show_timer = 5000; 
                } 
                else if (current_selection == 1) {
                    // 占位符：网络同步（还没写完联网代码，先弹个假提示）
                    Screen_Clear();
                    Screen_DrawHeader();
                    Screen_ShowChineseLine(20, 100, "网络未配置...");
                    Screen_Update();
                    delay(1500);
                    Render_Menu_UI(current_selection); // 提示完退回菜单
                }
                else if (current_selection == 2) {
                    // 立即进入黑屏休眠
                    Screen_Clear();
                    Screen_Update();
                    current_state = STATE_SLEEP;
                }
                else if (current_selection == 3) {
                    // 返回立绘待机
                    current_state = STATE_STANDBY;
                    Screen_Clear();
                    Screen_DrawStandbyImage();
                }
            }
            last_tick = millis(); 
        }
    } else {
        // ==========================================
        // 3. 处理自动超时事件 (放着不管的逻辑)
        // ==========================================
        switch (current_state) {
            case STATE_SHOWING_TEXT:
                if (text_show_timer > delta_time) {
                    text_show_timer -= delta_time;
                } else {
                    Screen_Clear();
                    Screen_DrawStandbyImage(); 
                    current_state = STATE_STANDBY;
                    idle_timer = 0; 
                }
                break;
                
            case STATE_MENU:
                idle_timer += delta_time;
                if (idle_timer > 15000) { // 菜单里发呆 15 秒，自动退回立绘
                    Screen_Clear();
                    Screen_DrawStandbyImage();
                    current_state = STATE_STANDBY;
                    idle_timer = 0;
                }
                break;

            case STATE_STANDBY:
                idle_timer += delta_time;
                if (idle_timer > 30000) { // 立绘看 30 秒，自动黑屏休眠
                    Screen_Clear();
                    Screen_Update(); 
                    current_state = STATE_SLEEP;
                }
                break;
                
            case STATE_SLEEP:
                break;
        }
    }
    delay(10); 
}