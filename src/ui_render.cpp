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
    STATE_SLEEP_SETTING
} SystemState;

static SystemState current_state = STATE_STANDBY;
static uint32_t idle_timer = 0;
static uint32_t last_tick = 0;

static int current_selection = 0;      
static float visual_selection = 0.0f;  
const int menu_count = 5; 

static uint32_t config_sleep_time_ms = 30000; 
static int sleep_opt_idx = 0; 

// === 按键逻辑变量 ===
static uint32_t btn_press_start_time = 0;
static bool btn_is_holding = false;
static bool long_press_handled = false; 
const uint32_t LONG_PRESS_THRESHOLD = 600; 

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
    
    // 1. 旋钮处理
    if (knob_delta != 0) {
        idle_timer = 0;
        if (current_state == STATE_MENU) {
            current_selection = (current_selection + knob_delta + menu_count) % menu_count;
            HAL_Buzzer_Random_Glitch(); 
        }
        else if (current_state == STATE_SLEEP_SETTING) {
            sleep_opt_idx = (sleep_opt_idx + knob_delta + 4) % 4;
            UI_DrawSleepSetting(sleep_opt_idx);
            HAL_Buzzer_Random_Glitch();
        }
    }

    // 2. 菜单动画引擎
    if (current_state == STATE_MENU) {
        float target = (float)current_selection;
        float diff = target - visual_selection;
        if (abs(diff) > 0.01f) {
            // 如果距离太大（例如首尾跳转），则直接瞬间对齐，消除“突兀”的长距离滑行感
            if (abs(diff) > 1.5f) {
                visual_selection = target;
            } else {
                visual_selection += diff * 0.25f; 
            }
            UI_DrawMenu_Animated(visual_selection);
        }
    }

    // 3. 改进型按键检测逻辑
    bool is_pressed = HAL_Is_Key_Pressed();

    if (is_pressed) {
        if (!btn_is_holding) {
            btn_press_start_time = current_time;
            btn_is_holding = true;
            long_press_handled = false;
        } 
        else if (!long_press_handled && (current_time - btn_press_start_time > LONG_PRESS_THRESHOLD)) {
            // --- 触发长按逻辑 ---
            long_press_handled = true;
            HAL_Buzzer_Play_Tone(800, 150); 
            
            if (current_state == STATE_MENU) {
                // 【规则】：主菜单界面长按 -> 退回待机
                current_state = STATE_STANDBY;
                HAL_Screen_Clear();
                HAL_Screen_DrawStandbyImage();
            }
            else if (current_state == STATE_SLEEP_SETTING || current_state == STATE_SHOWING_TEXT) {
                // 【规则】：设定界面或指令界面长按 -> 退回主菜单
                current_state = STATE_MENU;
                HAL_Screen_Clear();
                HAL_Screen_DrawHeader();
                UI_DrawMenu_Animated(visual_selection);
            }
        }
    } 
    else {
        if (btn_is_holding) {
            uint32_t duration = current_time - btn_press_start_time;
            btn_is_holding = false;

            if (!long_press_handled && duration > 50) {
                // --- 触发短按逻辑 (确认 / 重新抽取) ---
                idle_timer = 0;

                if (current_state == STATE_STANDBY || current_state == STATE_SLEEP) {
                    current_state = STATE_MENU;
                    current_selection = 0;
                    visual_selection = 0.0f;
                    HAL_Screen_Clear();
                    HAL_Screen_DrawHeader();
                    UI_DrawMenu_Animated(visual_selection);
                    HAL_Buzzer_Play_Tone(2000, 40);
                }
                else if (current_state == STATE_MENU) {
                    HAL_Buzzer_Play_Tone(2500, 80);
                    if (current_selection == 0) {
                        Prescript_Action();
                        current_state = STATE_SHOWING_TEXT; 
                    } 
                    else if (current_selection == 1) {
                        UI_DrawNetworkSyncScreen();
                        delay(1500);
                        HAL_Screen_Clear();
                        HAL_Screen_DrawHeader();
                        UI_DrawMenu_Animated(visual_selection);
                    }
                    else if (current_selection == 2) {
                        UI_Toggle_Language();
                        UI_DrawMenu_Animated(visual_selection);
                    }
                    else if (current_selection == 3) {
                        current_state = STATE_SLEEP_SETTING;
                        UI_DrawSleepSetting(sleep_opt_idx);
                    }
                    else if (current_selection == 4) {
                        current_state = STATE_STANDBY;
                        HAL_Screen_Clear();
                        HAL_Screen_DrawStandbyImage();
                    }
                }
                else if (current_state == STATE_SHOWING_TEXT) {
                    // 短按：重新抽取指令
                    HAL_Buzzer_Play_Tone(2200, 40);
                    Prescript_Action(); 
                }
                else if (current_state == STATE_SLEEP_SETTING) {
                    // 短按：保存并返回
                    HAL_Buzzer_Play_Tone(2500, 80);
                    switch(sleep_opt_idx) {
                        case 0: config_sleep_time_ms = 30000; break;
                        case 1: config_sleep_time_ms = 60000; break;
                        case 2: config_sleep_time_ms = 300000; break;
                        case 3: config_sleep_time_ms = 0xFFFFFFFF; break;
                    }
                    current_state = STATE_MENU;
                    HAL_Screen_Clear();
                    HAL_Screen_DrawHeader();
                    UI_DrawMenu_Animated(visual_selection);
                }
            }
        }
    }

    // 4. 超时处理与自动休眠
    if (current_state == STATE_MENU || current_state == STATE_SLEEP_SETTING) {
        idle_timer += delta_time;
        if (idle_timer > 15000) { 
            HAL_Screen_Clear();
            HAL_Screen_DrawStandbyImage();
            current_state = STATE_STANDBY;
            idle_timer = 0;
        }
    }
    else if (current_state == STATE_STANDBY) {
        idle_timer += delta_time;
        if (config_sleep_time_ms != 0xFFFFFFFF && idle_timer > config_sleep_time_ms) {
            HAL_Screen_Clear();
            HAL_Screen_Update(); 
            current_state = STATE_SLEEP;
        }
    }
    
    delay(10); 
}