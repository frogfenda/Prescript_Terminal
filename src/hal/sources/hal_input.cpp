/*
【模块职责】旋钮和实体按键输入。
【边界】只采集 GPIO 和产生离散输入事件，不绘制、不休眠、不调用 SYS 服务。
*/
#include "hal/hal.h"
#include "hal/hal_input_config.h"
#include "hal_internal.h"

#include "bsp/bsp_pins.h"

namespace
{
    volatile int raw_knob_counter = 0;

    IRAM_ATTR void ISR_Knob_Turn()
    {
        static uint8_t old_AB = 3;
        static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1,
                                            -1, 0, 0, 1, 0, 1, -1, 0};
        const uint8_t a = digitalRead(BSP::Pins::KNOB_A);
        const uint8_t b = digitalRead(BSP::Pins::KNOB_B);
        old_AB <<= 2;
        old_AB |= ((a << 1) | b);
        raw_knob_counter += enc_states[old_AB & 0x0f];
    }

    class ButtonEngine
    {
    private:
        uint32_t press_time = 0;
        uint32_t release_time = 0;
        uint32_t raw_change_time = 0;
        bool is_pressed = false;
        bool wait_double = false;
        bool long_triggered = false;
        bool raw_state = false;
        bool stable_state = false;

        uint32_t long_press_ms;
        uint32_t double_gap_ms;
        uint32_t debounce_ms;
        bool enable_double_click;

    public:
        ButtonEngine(uint32_t lp_ms, uint32_t dg_ms, bool enable_dbl, uint32_t db_ms)
            : long_press_ms(lp_ms), double_gap_ms(dg_ms), debounce_ms(db_ms), enable_double_click(enable_dbl)
        {
        }

        void reset(bool raw_pressed = false)
        {
            is_pressed = raw_pressed;
            wait_double = false;
            long_triggered = raw_pressed;
            raw_state = raw_pressed;
            stable_state = raw_pressed;
            press_time = millis();
            release_time = 0;
            raw_change_time = millis();
        }

        BtnEvent update(bool raw_pressed)
        {
            const uint32_t now = millis();
            if (raw_pressed != raw_state)
            {
                raw_state = raw_pressed;
                raw_change_time = now;
            }

            bool current_state = stable_state;
            if (raw_state != stable_state && now - raw_change_time >= debounce_ms)
            {
                stable_state = raw_state;
                current_state = stable_state;
            }

            if (current_state && !is_pressed)
            {
                is_pressed = true;
                press_time = now;
                long_triggered = false;
                if (enable_double_click && wait_double)
                {
                    wait_double = false;
                    long_triggered = true;
                    return BTN_DOUBLE;
                }
            }
            else if (!current_state && is_pressed)
            {
                is_pressed = false;
                const uint32_t duration = now - press_time;
                if (!long_triggered && duration > debounce_ms)
                {
                    if (enable_double_click)
                    {
                        wait_double = true;
                        release_time = now;
                    }
                    else
                    {
                        return BTN_SHORT;
                    }
                }
            }
            else if (current_state && is_pressed)
            {
                if (!long_triggered && now - press_time > long_press_ms)
                {
                    long_triggered = true;
                    wait_double = false;
                    return BTN_LONG;
                }
            }
            else if (!current_state && !is_pressed)
            {
                if (enable_double_click && wait_double && now - release_time > double_gap_ms)
                {
                    wait_double = false;
                    return BTN_SHORT;
                }
            }
            return BTN_NONE;
        }
    };

    ButtonEngine engineMainBtn(HAL::InputConfig::BUTTON_LONG_MS,
                               HAL::InputConfig::BUTTON_DOUBLE_GAP_MS,
                               false,
                               HAL::InputConfig::BUTTON_MAIN_DEBOUNCE_MS);
    ButtonEngine engineBtn2(HAL::InputConfig::BUTTON_LONG_MS,
                            HAL::InputConfig::BUTTON_DOUBLE_GAP_MS,
                            true,
                            HAL::InputConfig::BUTTON_SIDE_DEBOUNCE_MS);
}

void HAL_Input_Init()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    pinMode(BSP::Pins::BTN_MAIN, INPUT_PULLUP);
    pinMode(BSP::Pins::KNOB_A, INPUT_PULLUP);
    pinMode(BSP::Pins::KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BSP::Pins::KNOB_A), ISR_Knob_Turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BSP::Pins::KNOB_B), ISR_Knob_Turn, CHANGE);
    pinMode(BSP::Pins::BTN_SIDE, INPUT_PULLUP);
    engineMainBtn.reset(false);
    engineBtn2.reset(false);
}

void HAL_Input_ResetAfterWake()
{
    engineMainBtn.reset(digitalRead(BSP::Pins::BTN_MAIN) == LOW);
    engineBtn2.reset(digitalRead(BSP::Pins::BTN_SIDE) == LOW);
}

void HAL_Btn2_Init()
{
    pinMode(BSP::Pins::BTN_SIDE, INPUT_PULLUP);
}

bool HAL_Is_Key_Pressed()
{
    return digitalRead(BSP::Pins::BTN_MAIN) == LOW;
}

int HAL_Get_Knob_Delta()
{
    int raw = 0;
    noInterrupts();
    raw = raw_knob_counter;
    const int delta = raw / 4;
    if (delta != 0)
        raw_knob_counter -= delta * 4;
    interrupts();
    return delta;
}

BtnEvent HAL_Get_Btn_Main_Event()
{
    return engineMainBtn.update(HAL_Is_Key_Pressed());
}

BtnEvent HAL_Get_Btn2_Event()
{
    return engineBtn2.update(digitalRead(BSP::Pins::BTN_SIDE) == LOW);
}
