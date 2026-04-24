// 文件：src/hal/hal.cpp
#include "hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h>
#include "esp_sleep.h"
#include "driver/gpio.h"       // 【新增】：为了使用 gpio_hold_en 和 gpio_hold_dis
#include <driver/i2s.h>        // 【核心新增】：ESP32 I2S 底层驱动
#include "../sys/sys_config.h" // 【新增】：引入全局配置
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite textSprite = TFT_eSprite(&tft);
U8g2_for_TFT_eSPI u8f;

volatile int raw_knob_counter = 0;

IRAM_ATTR void ISR_Knob_Turn()
{
    static uint8_t old_AB = 3;
    static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t A = digitalRead(PIN_KNOB_A);
    uint8_t B = digitalRead(PIN_KNOB_B);
    old_AB <<= 2;
    old_AB |= ((A << 1) | B);
    raw_knob_counter += enc_states[(old_AB & 0x0f)];
}

void HAL_Init()
{
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // ==========================================
    // 【新增】：背光与功放硬件初始化，防止引脚高阻态
    // ==========================================
    pinMode(PIN_BLK, OUTPUT);
    digitalWrite(PIN_BLK, LOW); // 默认点亮屏幕（接GND亮）

    pinMode(PIN_AUDIO_SD, OUTPUT);
    digitalWrite(PIN_AUDIO_SD, HIGH); // 默认开启功放（给高电平开）
    // ==========================================

    textSprite.setColorDepth(16);
    uint16_t sw = HAL_Get_Screen_Width();
    uint16_t sh = HAL_Get_Screen_Height();
    void *ptr = textSprite.createSprite(sw, sh);
    if (ptr == NULL)
        Serial.println("!!! Sprite 内存不足 !!!");

    textSprite.fillSprite(TFT_BLACK);
    textSprite.setTextWrap(false);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_B), ISR_Knob_Turn, CHANGE);
    HAL_Btn2_Init();
    u8f.begin(textSprite);
    u8f.setFontMode(1);
    u8f.setFontDirection(0);
    u8f.setBackgroundColor(TFT_BLACK);
    u8f.setFont(u8g2_font_wqy12_t_gb2312);
}

int HAL_Get_Knob_Delta(void)
{
    int delta = raw_knob_counter / 4;
    if (delta != 0)
        raw_knob_counter -= delta * 4;
    return delta;
}
bool HAL_Is_Key_Pressed() { return digitalRead(PIN_BTN) == LOW; }

void HAL_Screen_Clear()
{
    tft.fillScreen(TFT_BLACK);
    textSprite.fillSprite(TFT_BLACK);
}

void HAL_Screen_DrawHeader()
{
    textSprite.setTextColor(TFT_RED, TFT_BLACK);
    textSprite.setTextSize(1);
    textSprite.setCursor(10, 8);
    textSprite.print("[ PRESCRIPT ]");
}

void HAL_Screen_DrawStandbyImage()
{
    textSprite.fillSprite(TFT_BLACK);
    File file = LittleFS.open("/assets/standby.bin", "r");
    if (!file)
    {
        textSprite.drawRect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_RED);
        textSprite.setTextColor(TFT_RED, TFT_BLACK);
        textSprite.drawString("ERR: NO standby.bin", 10, 10);
        return;
    }
    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (sprite_ptr != nullptr)
    {
        size_t bytes_to_read = HAL_Get_Screen_Width() * HAL_Get_Screen_Height() * 2;
        file.read((uint8_t *)sprite_ptr, bytes_to_read);
    }
    file.close();
}

void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char *str)
{
    textSprite.setTextColor(TFT_CYAN, TFT_BLACK);
    textSprite.setTextSize(1);
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char *str)
{
    u8f.setForegroundColor(TFT_CYAN);
    u8f.setCursor(x, y + 12);
    u8f.print(str);
}

int HAL_Get_Text_Width(const char *str) { return u8f.getUTF8Width(str); }

void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char *str, float distance)
{
    HAL_Screen_ShowChineseLine_Faded_Color(x, y, str, distance, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char *str, float distance, uint16_t base_color)
{
    float intensity = 1.0f - (distance * 0.40f);
    if (intensity < 0.15f)
        intensity = 0.15f;

    uint8_t r8 = ((base_color >> 11) & 0x1F) * 255 / 31;
    uint8_t g8 = ((base_color >> 5) & 0x3F) * 255 / 63;
    uint8_t b8 = (base_color & 0x1F) * 255 / 31;

    uint8_t final_r = (uint8_t)(r8 * intensity);
    uint8_t final_g = (uint8_t)(g8 * intensity);
    uint8_t final_b = (uint8_t)(b8 * intensity);

    uint16_t faded_color = tft.color565(final_r, final_g, final_b);

    u8f.setForegroundColor(faded_color);
    u8f.setCursor(x, y + 12);
    u8f.print(str);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }

void HAL_Screen_Update()
{
    textSprite.pushSprite(18, 82);
}

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.drawLine(x0, y0, x1, y1, color);
}
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.drawRect(x, y, w, h, color);
}
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.fillRect(x, y, w, h, color);
}
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color)
{
    if (color == 1)
        color = TFT_CYAN;
    textSprite.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color)
{
    textSprite.drawPixel(x, y, color);
}
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h)
{
    textSprite.pushSprite(x + 18, y + 82, x, y, w, h);
}
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
    textSprite.setSwapBytes(true);
    textSprite.pushImage(x, y, w, h, data);
    textSprite.setSwapBytes(false);
}

// ==========================================
// 【核心解耦】：通用按键多态状态机引擎 (极速响应版)
// ==========================================
class ButtonEngine
{
private:
    uint32_t press_time = 0;
    uint32_t release_time = 0;
    bool is_pressed = false;
    bool wait_double = false;
    bool long_triggered = false;

    uint32_t long_press_ms;
    uint32_t double_gap_ms;
    bool enable_double_click; // 【新增】：双击使能开关

public:
    // 构造函数：默认开启双击。如果传 false，则松手瞬间立刻判定短按！
    ButtonEngine(uint32_t lp_ms = 800, uint32_t dg_ms = 250, bool enable_dbl = true)
    {
        long_press_ms = lp_ms;
        double_gap_ms = dg_ms;
        enable_double_click = enable_dbl;
    }

    // 【新增】：强制重置状态机，用于吞掉休眠唤醒时的残余按键
    void reset()
    {
        is_pressed = false;
        wait_double = false;
        long_triggered = false;
    }

    BtnEvent update(bool current_state)
    {
        uint32_t now = millis();
        BtnEvent result = BTN_NONE;

        if (current_state && !is_pressed)
        {
            is_pressed = true;
            press_time = now;
            long_triggered = false;
        }
        else if (!current_state && is_pressed)
        {
            is_pressed = false;
            uint32_t duration = now - press_time;

            if (!long_triggered && duration > 20)
            { // 20ms 硬件防抖
                if (enable_double_click)
                {
                    // 如果开启了双击，就挂起等待
                    if (wait_double)
                    {
                        wait_double = false;
                        result = BTN_DOUBLE;
                    }
                    else
                    {
                        wait_double = true;
                        release_time = now;
                    }
                }
                else
                {
                    // 【核心修复】：如果不需要双击，松手的瞬间立刻开火！零延迟！
                    result = BTN_SHORT;
                }
            }
        }
        else if (current_state && is_pressed)
        {
            if (!long_triggered && (now - press_time > long_press_ms))
            {
                long_triggered = true;
                wait_double = false;
                result = BTN_LONG;
            }
        }
        else if (!current_state && !is_pressed)
        {
            // 只有开启双击的情况下，才会走到这个超时判定的逻辑
            if (enable_double_click && wait_double && (now - release_time > double_gap_ms))
            {
                wait_double = false;
                result = BTN_SHORT;
            }
        }
        return result;
    }
};

// ==========================================
// 【完美实例化】：各司其职的按键配置
// ==========================================
// 旋钮主按键：关闭双击 (传入 false)，恢复绝对丝滑的零延迟响应！
ButtonEngine engineMainBtn(800, 250, false);

// 副按键 (7号引脚)：开启双击 (传入 true)，承担复杂的宏指令调度！
ButtonEngine engineBtn2(800, 250, true);

// ==========================================
// 【休眠系统原子化】：将休眠拆解，供 AppStandby 统一调度
// ==========================================
void HAL_Sleep_Enter_Prepare()
{
    // 1. 熄灭背光与关断功放
    digitalWrite(PIN_BLK, HIGH);
    gpio_hold_en((gpio_num_t)PIN_BLK);
    digitalWrite(PIN_AUDIO_SD, LOW);
    gpio_hold_en((gpio_num_t)PIN_AUDIO_SD);
    gpio_deep_sleep_hold_en();

    // 2. 屏幕驱动 IC 内部挂起
    tft.writecommand(0x10);
}

void HAL_Sleep_Start()
{
    // 3. 真正的浅睡眠触发
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);
    esp_light_sleep_start();
}

void HAL_Sleep_Wakeup_Post()
{
    // 1. 唤醒屏幕驱动 IC
    tft.writecommand(0x11);

    // 2. 【核心优化】：趁着灯还没亮，赶紧把待机图刷进屏幕显存 (GRAM)
    // 这样开灯的一瞬间，画面就是完整的，而不是黑屏或噪点
    HAL_Screen_DrawStandbyImage();
    HAL_Screen_Update();

    // 3. 必须等待 120ms，让 ST7789 内部的电荷泵和液晶分子稳定
    delay(120);

    // 4. 画面稳了，再解锁并点亮背光
    gpio_hold_dis((gpio_num_t)PIN_BLK);
    gpio_hold_dis((gpio_num_t)PIN_AUDIO_SD);
    digitalWrite(PIN_AUDIO_SD, HIGH);
    digitalWrite(PIN_BLK, LOW); // 灯亮，画面瞬间浮现

    // 5. 唤醒外设
    extern void SysHaptic_Wakeup();
    SysHaptic_Wakeup();
    extern void SysAudio_Wakeup();
    SysAudio_Wakeup();
    extern void SysNfc_Wakeup();
    SysNfc_Wakeup();

    // 6. 防误触：吞掉唤醒时的那次点击
    while (digitalRead(PIN_BTN) == LOW || digitalRead(PIN_BTN2) == LOW)
    {
        delay(10);
    }
    engineMainBtn.reset();
    engineBtn2.reset();
}

void HAL_Btn2_Init()
{
    pinMode(PIN_BTN2, INPUT_PULLUP);
}

BtnEvent HAL_Get_Btn_Main_Event()
{
    extern bool HAL_Is_Key_Pressed();
    return engineMainBtn.update(HAL_Is_Key_Pressed());
}

BtnEvent HAL_Get_Btn2_Event()
{
    // 读取引脚并喂给状态机引擎
    BtnEvent evt = engineBtn2.update(digitalRead(PIN_BTN2) == LOW);

    // 【物理雷达】：只要硬件没接错，按下去必定会打印！
    if (evt == BTN_SHORT)
        Serial.println("[硬件层] 侦测到 Btn2: 短按");
    else if (evt == BTN_DOUBLE)
        Serial.println("[硬件层] 侦测到 Btn2: 双击");
    else if (evt == BTN_LONG)
        Serial.println("[硬件层] 侦测到 Btn2: 长按");

    return evt;
}

void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char* str, uint16_t color) {
    u8f.setForegroundColor(color);
    u8f.setCursor(x, y + 12);
    u8f.print(str);
}
void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char* str, uint16_t color) {
    textSprite.setTextColor(color, TFT_BLACK);
    textSprite.setTextSize(1);
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }
uint16_t HAL_Get_Screen_Width(void) { return 284; }
uint16_t HAL_Get_Screen_Height(void) { return 76; }