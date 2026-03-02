#include "app_fsm.h"
#include "hal.h"
#include "prescript_logic.h"
#include <Arduino.h>

typedef enum {
    STATE_SLEEP = 0,
    STATE_STANDBY,
    STATE_SHOWING_TEXT
} SystemState;

static SystemState current_state = STATE_STANDBY;
static uint32_t text_show_timer = 0;
static uint32_t idle_timer = 0;
static uint32_t last_tick = 0;

void App_FSM_Init(void) {
    HAL_Init();
    // 强制更新硬件随机数种子，防止每次开机抽卡顺序一样
    randomSeed(analogRead(A0)); 
    current_state = STATE_STANDBY;
    last_tick = millis();
}

void App_FSM_Run(void) {
    uint32_t current_time = millis();
    uint32_t delta_time = current_time - last_tick;
    last_tick = current_time;

    if (Is_Key_Pressed()) {
        delay(20); 
        if (Is_Key_Pressed()) {
            while(Is_Key_Pressed()) delay(10); 
            
            if (current_state == STATE_SLEEP) {
                current_state = STATE_STANDBY;
                idle_timer = 0;
                Screen_Clear();
                Screen_DrawHeader();
                Screen_Update();
            } else {
                Prescript_Action(); 
                current_state = STATE_SHOWING_TEXT;
                text_show_timer = 5000; // 停留 5 秒
            }
        }
    } else {
        switch (current_state) {
            case STATE_SHOWING_TEXT:
                if (text_show_timer > delta_time) {
                    text_show_timer -= delta_time;
                } else {
                    Screen_Clear();
                    Screen_DrawHeader();
                    Screen_Update();
                    current_state = STATE_STANDBY;
                    idle_timer = 0; 
                }
                break;
                
            case STATE_STANDBY:
                idle_timer += delta_time;
                if (idle_timer > 10000) { // 10秒后休眠
                    Screen_Clear();
                    Screen_Update();
                    current_state = STATE_SLEEP;
                }
                break;
                
            case STATE_SLEEP:
                // 屏幕全黑，等待按键唤醒
                break;
        }
    }
    delay(10); 
}