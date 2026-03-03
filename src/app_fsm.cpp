#include "app_fsm.h"
#include "hal.h"
#include "prescript_logic.h"
#include <Arduino.h>

typedef enum {
    STATE_SLEEP = 0,
    STATE_STANDBY,
    STATE_MENU,          
    STATE_SHOWING_TEXT
} SystemState;

static SystemState current_state = STATE_STANDBY;
static uint32_t text_show_timer = 0;
static uint32_t idle_timer = 0;
static uint32_t last_tick = 0;

static int current_selection = 0;
const int menu_count = 5; // 【变成5个选项！】

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

    int knob_delta = Get_Knob_Delta();
    if (current_state == STATE_MENU && knob_delta != 0) {
        current_selection += knob_delta;
        if (current_selection < 0) current_selection = menu_count - 1;
        if (current_selection >= menu_count) current_selection = 0;
        
        Screen_DrawMenu(current_selection); 
        Buzzer_Random_Glitch(); 
        idle_timer = 0;         
    }

    if (Is_Key_Pressed()) {
        delay(20); 
        if (Is_Key_Pressed()) {
            while(Is_Key_Pressed()) { delay(10); yield(); } 
            idle_timer = 0;

            if (current_state == STATE_SLEEP) {
                current_state = STATE_STANDBY;
                Screen_Clear();
                Screen_DrawStandbyImage();
            } 
            else if (current_state == STATE_STANDBY) {
                current_state = STATE_MENU;
                current_selection = 0; 
                Screen_DrawMenu(current_selection);
                Buzzer_Play_Tone(2000, 40); 
            }
            else if (current_state == STATE_MENU) {
                Buzzer_Play_Tone(2500, 80); 
                
                if (current_selection == 0) {
                    // 执行程序
                    Prescript_Action();
                    current_state = STATE_SHOWING_TEXT;
                    text_show_timer = 5000; 
                } 
                else if (current_selection == 1) {
                    // 网络同步
                    Screen_Clear();
                    Screen_DrawHeader();
                    // 动态提示语言
                    Screen_ShowChineseLine(40, 100, (Get_Language() == LANG_ZH) ? "网络未配置..." : "Network Offline..."); 
                    Screen_Update();
                    delay(1500);
                    Screen_DrawMenu(current_selection); 
                }
                else if (current_selection == 2) {
                    // 【全新功能：一键切换语言！】
                    Toggle_Language();
                    Screen_DrawMenu(current_selection); // 原地瞬间刷新菜单
                }
                else if (current_selection == 3) {
                    // 休眠
                    Screen_Clear();
                    Screen_Update();
                    current_state = STATE_SLEEP;
                }
                else if (current_selection == 4) {
                    // 返回
                    current_state = STATE_STANDBY;
                    Screen_Clear();
                    Screen_DrawStandbyImage();
                }
            }
            last_tick = millis(); 
        }
    } else {
        switch (current_state) {
            case STATE_SHOWING_TEXT:
                if (text_show_timer > delta_time) text_show_timer -= delta_time;
                else {
                    Screen_Clear();
                    Screen_DrawStandbyImage(); 
                    current_state = STATE_STANDBY;
                    idle_timer = 0; 
                }
                break;
            case STATE_MENU:
                idle_timer += delta_time;
                if (idle_timer > 15000) { 
                    Screen_Clear();
                    Screen_DrawStandbyImage();
                    current_state = STATE_STANDBY;
                    idle_timer = 0;
                }
                break;
            case STATE_STANDBY:
                idle_timer += delta_time;
                if (idle_timer > 30000) { 
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