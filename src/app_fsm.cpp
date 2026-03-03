#include "app_fsm.h"
#include "hal.h"
#include "ui_render.h" 
#include "prescript_logic.h"
#include <Arduino.h>

typedef enum {
    STATE_SLEEP = 0,
    STATE_STANDBY,
    STATE_MENU,          
    STATE_SHOWING_TEXT,
    STATE_SLEEP_SETTING   // 【新增】：休眠设定状态
} SystemState;

static SystemState current_state = STATE_STANDBY;
static uint32_t idle_timer = 0;
static uint32_t last_tick = 0;

static int current_selection = 0;
const int menu_count = 5; 

// 【新增】：休眠配置变量
static uint32_t config_sleep_time_ms = 30000; // 默认30秒
static int sleep_opt_idx = 0; // 0=30s, 1=60s, 2=300s, 3=永不

void App_FSM_Init(void) {
    HAL_Init();
    randomSeed(analogRead(A0) ^ micros()); 
    current_state = STATE_STANDBY;
    last_tick = millis();

    HAL_Screen_Clear();
    HAL_Screen_DrawStandbyImage(); 
}

void App_FSM_Run(void) {
    uint32_t current_time = millis();
    uint32_t delta_time = current_time - last_tick;
    last_tick = current_time;

    int knob_delta = HAL_Get_Knob_Delta();
    
    // 处理旋钮事件
    if (knob_delta != 0) {
        if (current_state == STATE_MENU) {
            current_selection += knob_delta;
            if (current_selection < 0) current_selection = menu_count - 1;
            if (current_selection >= menu_count) current_selection = 0;
            
            UI_DrawMenu(current_selection); 
            HAL_Buzzer_Random_Glitch(); 
            idle_timer = 0;         
        }
        else if (current_state == STATE_SLEEP_SETTING) {
            // 在设定界面转动旋钮
            sleep_opt_idx += knob_delta;
            if (sleep_opt_idx < 0) sleep_opt_idx = 3; // 4个选项
            if (sleep_opt_idx > 3) sleep_opt_idx = 0;
            
            UI_DrawSleepSetting(sleep_opt_idx);
            HAL_Buzzer_Random_Glitch();
            idle_timer = 0;
        }
    }

    // 处理按键事件
    if (HAL_Is_Key_Pressed()) {
        delay(20); 
        if (HAL_Is_Key_Pressed()) {
            while(HAL_Is_Key_Pressed()) { delay(10); yield(); } 
            idle_timer = 0;

            if (current_state == STATE_SLEEP) {
                current_state = STATE_STANDBY;
                HAL_Screen_Clear();
                HAL_Screen_DrawStandbyImage();
            } 
            else if (current_state == STATE_STANDBY) {
                current_state = STATE_MENU;
                current_selection = 0; 
                UI_DrawMenu(current_selection);
                HAL_Buzzer_Play_Tone(2000, 40); 
            }
            else if (current_state == STATE_SHOWING_TEXT) {
                // 【修改 1】：代行者已阅指令，按键后直接返回主菜单
                HAL_Buzzer_Play_Tone(2000, 40);
                current_state = STATE_MENU;
                UI_DrawMenu(current_selection); 
            }
            else if (current_state == STATE_SLEEP_SETTING) {
                // 【新增】：确认休眠时间设定
                HAL_Buzzer_Play_Tone(2500, 80); 
                switch(sleep_opt_idx) {
                    case 0: config_sleep_time_ms = 30000; break;
                    case 1: config_sleep_time_ms = 60000; break;
                    case 2: config_sleep_time_ms = 300000; break;
                    case 3: config_sleep_time_ms = 0xFFFFFFFF; break; // 0xFFFFFFFF 代表永不休眠
                }
                current_state = STATE_MENU;
                UI_DrawMenu(current_selection);
            }
            else if (current_state == STATE_MENU) {
                HAL_Buzzer_Play_Tone(2500, 80); 
                
                if (current_selection == 0) {
                    Prescript_Action();
                    current_state = STATE_SHOWING_TEXT; 
                    // 【修改 2】：去掉了 timer 倒计时，指令将永久留存在屏幕上
                } 
                else if (current_selection == 1) {
                    UI_DrawNetworkSyncScreen();
                    delay(1500);
                    UI_DrawMenu(current_selection); 
                }
                else if (current_selection == 2) {
                    UI_Toggle_Language();
                    UI_DrawMenu(current_selection);
                }
                else if (current_selection == 3) {
                    // 进入休眠时间设定界面
                    current_state = STATE_SLEEP_SETTING;
                    UI_DrawSleepSetting(sleep_opt_idx);
                }
                else if (current_selection == 4) {
                    current_state = STATE_STANDBY;
                    HAL_Screen_Clear();
                    HAL_Screen_DrawStandbyImage();
                }
            }
            last_tick = millis(); 
        }
    } else {
        // 处理无操作超时逻辑
        switch (current_state) {
            case STATE_SHOWING_TEXT:
                // 指令界面不再记录 idle_timer，强迫症福音，必须手动按键确认！
                idle_timer = 0; 
                break;
            case STATE_MENU:
            case STATE_SLEEP_SETTING:
                idle_timer += delta_time;
                if (idle_timer > 15000) {  // 菜单界面 15 秒不操作退回待机画面
                    HAL_Screen_Clear();
                    HAL_Screen_DrawStandbyImage();
                    current_state = STATE_STANDBY;
                    idle_timer = 0;
                }
                break;
            case STATE_STANDBY:
                idle_timer += delta_time;
                // 如果设定了“永不休眠”，则 config_sleep_time_ms 为最大值，永远不会触发此处
                if (config_sleep_time_ms != 0xFFFFFFFF && idle_timer > config_sleep_time_ms) { 
                    HAL_Screen_Clear();
                    HAL_Screen_Update(); 
                    current_state = STATE_SLEEP;
                }
                break;
            case STATE_SLEEP:
                break;
        }
    }
    delay(10); 
}