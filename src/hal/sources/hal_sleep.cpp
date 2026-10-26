/*
【模块职责】显示、电源和 GPIO 唤醒相关的 Light Sleep 硬件序列。
【边界】这里只配置底层唤醒源和板级电源/屏幕状态；SYS 外设的休眠与恢复由待机 App 调度。
*/
#include "hal/hal.h"
#include "hal_internal.h"

#include "bsp/bsp_display_nv3007.h"
#include "bsp/bsp_pins.h"
#include "bsp/bsp_power.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

namespace
{
    static esp_err_t RestoreWakeInputs()
    {
        gpio_config_t wake_config = {};
        wake_config.pin_bit_mask = (1ULL << BSP::Pins::BTN_MAIN) |
                                   (1ULL << BSP::Pins::BTN_SIDE) |
                                   (1ULL << BSP::Pins::RTC_INT);
        wake_config.mode = GPIO_MODE_INPUT;
        wake_config.pull_up_en = GPIO_PULLUP_ENABLE;
        wake_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        wake_config.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&wake_config);
    }
}

void HAL_Sleep_Enter_Prepare()
{
    BSP::Power::SetBacklight(false);
    BSP::Power::SetAudioAmp(false);
    BSP::Power::HoldBacklightAndAudio();
    BSP::DisplayNv3007::Sleep();
}

HALSleepWakeReason HAL_Sleep_Start(uint64_t timer_wakeup_us)
{
    const gpio_num_t main_button = (gpio_num_t)BSP::Pins::BTN_MAIN;
    const gpio_num_t side_button = (gpio_num_t)BSP::Pins::BTN_SIDE;
    const gpio_num_t rtc_interrupt = (gpio_num_t)BSP::Pins::RTC_INT;

    const esp_err_t input_config_err = RestoreWakeInputs();
    if (input_config_err != ESP_OK)
    {
        Serial.printf("[休眠] 休眠前唤醒 GPIO 配置失败：错误码=%d，本次不进入休眠。\n",
                      (int)input_config_err);
        return HALSleepWakeReason::Error;
    }

    static bool rtc_low_warning_reported = false;
    const bool rtc_wake_available = digitalRead(BSP::Pins::RTC_INT) == HIGH;
    if (!rtc_wake_available && !rtc_low_warning_reported)
    {
        Serial.println("[休眠-警告] RTC INT# 入睡前仍为低电平，本轮跳过 RTC GPIO 唤醒并使用 ESP 定时器兜底。");
        rtc_low_warning_reported = true;
    }
    else if (rtc_wake_available)
    {
        rtc_low_warning_reported = false;
    }

    const esp_err_t main_wakeup_err = gpio_wakeup_enable(main_button, GPIO_INTR_LOW_LEVEL);
    const esp_err_t side_wakeup_err = gpio_wakeup_enable(side_button, GPIO_INTR_LOW_LEVEL);
    const esp_err_t rtc_wakeup_err = rtc_wake_available
        ? gpio_wakeup_enable(rtc_interrupt, GPIO_INTR_LOW_LEVEL)
        : ESP_OK;
    const esp_err_t source_err = esp_sleep_enable_gpio_wakeup();
    esp_err_t timer_err = ESP_OK;
    if (timer_wakeup_us > 0)
        timer_err = esp_sleep_enable_timer_wakeup(timer_wakeup_us);

    if (main_wakeup_err != ESP_OK || side_wakeup_err != ESP_OK || rtc_wakeup_err != ESP_OK ||
        source_err != ESP_OK || timer_err != ESP_OK)
    {
        Serial.printf("[休眠-错误] 唤醒源配置失败：主键=%d，侧键=%d，RTC=%d，GPIO=%d，定时器=%d。\n",
                      (int)main_wakeup_err, (int)side_wakeup_err, (int)rtc_wakeup_err,
                      (int)source_err, (int)timer_err);

        gpio_wakeup_disable(main_button);
        gpio_wakeup_disable(side_button);
        gpio_wakeup_disable(rtc_interrupt);
        if (source_err == ESP_OK)
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        if (timer_wakeup_us > 0 && timer_err == ESP_OK)
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        RestoreWakeInputs();
        return HALSleepWakeReason::Error;
    }

    const esp_err_t sleep_err = esp_light_sleep_start();
    const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    const bool rtc_asserted = digitalRead(BSP::Pins::RTC_INT) == LOW;

    gpio_wakeup_disable(main_button);
    gpio_wakeup_disable(side_button);
    gpio_wakeup_disable(rtc_interrupt);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (timer_wakeup_us > 0)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

    const esp_err_t restore_err = RestoreWakeInputs();
    if (restore_err != ESP_OK)
        Serial.printf("[休眠] 唤醒 GPIO 普通输入模式恢复失败：错误码=%d。\n", (int)restore_err);

    if (sleep_err != ESP_OK)
    {
        Serial.printf("[休眠-错误] Light Sleep 未正常执行，错误码=%d。\n", (int)sleep_err);
        return HALSleepWakeReason::Error;
    }

    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER)
        return HALSleepWakeReason::Timer;
    if (wakeup_cause == ESP_SLEEP_WAKEUP_GPIO)
        return rtc_asserted ? HALSleepWakeReason::Rtc : HALSleepWakeReason::Button;

    Serial.printf("[休眠-警告] Light Sleep 返回了未处理的唤醒原因：%d。\n", (int)wakeup_cause);
    return HALSleepWakeReason::Error;
}

void HAL_Sleep_Wakeup_Post()
{
    BSP::Power::ReleaseBacklightAndAudio();
    BSP::Power::SetBacklight(false);
    BSP::Power::SetAudioAmp(false);
    BSP::DisplayNv3007::Wakeup();
    HAL_Input_ResetAfterWake();
}

void HAL_Sleep_Wakeup_Complete()
{
    BSP::DisplayNv3007::DisplayOn();
    delay(20);
    BSP::Power::SetAudioAmp(true);
    BSP::Power::SetBacklight(true);
}
