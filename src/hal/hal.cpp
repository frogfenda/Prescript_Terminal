/*
【模块职责】硬件抽象实现。负责 ST7789+U8g2 显示、旋钮 A/B 相中断计数、两个按键的短按/长按/双击识别、背光/功放使能、Light Sleep 前后的屏幕和外设恢复。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/hal/hal.cpp
#include "hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h>
#include "esp_sleep.h"
#include "driver/gpio.h"       // 【新增】：为了使用 gpio_hold_en 和 gpio_hold_dis
#include <driver/i2s.h>        // 【核心新增】：ESP32 I2S 底层驱动
#include "../sys/sys_config.h" // 【新增】：引入全局配置
#include "../sys/sys_haptic.h"
#include "../sys/sys_audio.h"
#include "../sys/sys_nfc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite textSprite = TFT_eSprite(&tft);
U8g2_for_TFT_eSPI u8f;

volatile int raw_knob_counter = 0;

/*
 * 将逻辑 UI 坐标转换为 NV3007 实际写屏坐标。
 *
 * UI_PUSH_X / UI_PUSH_Y 表示旧 284×76 Sprite 在新横屏 428×142 画面中的几何位置；
 * DISPLAY_RAM_OFFSET_X / DISPLAY_RAM_OFFSET_Y 表示 NV3007 控制器内部 GRAM 可视区偏移。
 *
 * 这两个偏移必须分开：
 * - 想调整画面是否视觉居中，改 UI_PUSH_X / UI_PUSH_Y；
 * - 想修正底部花屏、控制器写窗口错位，改 DISPLAY_RAM_OFFSET_*。
 */
static inline int16_t HAL_DisplayRawX(int16_t logical_x)
{
    return logical_x + PrescriptConst::DISPLAY_RAM_OFFSET_X;
}

static inline int16_t HAL_DisplayRawY(int16_t logical_y)
{
    return logical_y + PrescriptConst::DISPLAY_RAM_OFFSET_Y;
}

// 【新屏幕适配】2.79 寸 142×428 长条屏使用 NV3007/NV3006A1 类初始化序列。
// TFT_eSPI 当前仍按 ST7789 驱动建立 SPI 事务和基本绘图接口，随后这里补发屏厂例程里的
// NV3007 初始化命令。这样可以在不重写 HAL 推图体系的前提下，先让新屏完成点亮和 RGB565 显示。
static void HAL_NV3007_SendCmd(uint8_t cmd)
{
    tft.writecommand(cmd);
}

static void HAL_NV3007_SendData(uint8_t data)
{
    tft.writedata(data);
}

// 【函数说明】按厂家 STM32 例程移植 NV3007/NV3006A1 初始化序列。
// 关键点：
// 1. 例程标注面板有效分辨率为 142×428，默认 RGB565；
// 2. 原例程在 TFT_SET_ADD() 中给 column 加了 12 像素偏移；
// 3. 这里先只移植初始化序列，窗口偏移先不改 TFT_eSPI 底层，便于先验证点亮；
// 4. 初始化完成后 HAL_Init() 会再调用 setRotation(1)，让设备按横向带鱼屏使用。
static void HAL_NV3007_Init_142x428()
{
    auto nvCmd = HAL_NV3007_SendCmd;
    auto nvData = HAL_NV3007_SendData;

    Serial.println("[显示] 使用 NV3007/NV3006A1 142x428 初始化序列。厂家例程 column offset=12，当前为简单居中适配。");

    delay(100);
    delay(120);
    nvCmd(0xff);
    nvData(0xa5);
    nvCmd(0x9a);
    nvData(0x08);
    nvCmd(0x9b);
    nvData(0x08);
    nvCmd(0x9c);
    nvData(0xb0);
    nvCmd(0x9d);
    nvData(0x16);
    nvCmd(0x9e);
    nvData(0xc4);
    nvCmd(0x8f);
    nvData(0x55);
    nvData(0x04);
    nvCmd(0x84);
    nvData(0x90);
    nvCmd(0x83);
    nvData(0x7b);
    nvCmd(0x85);
    nvData(0x33);
    nvCmd(0x60);
    nvData(0x00);
    nvCmd(0x70);
    nvData(0x00);
    nvCmd(0x61);
    nvData(0x02);
    nvCmd(0x71);
    nvData(0x02);
    nvCmd(0x62);
    nvData(0x04);
    nvCmd(0x72);
    nvData(0x04);
    nvCmd(0x6c);
    nvData(0x29);
    nvCmd(0x7c);
    nvData(0x29);
    nvCmd(0x6d);
    nvData(0x31);
    nvCmd(0x7d);
    nvData(0x31);
    nvCmd(0x6e);
    nvData(0x0f);
    nvCmd(0x7e);
    nvData(0x0f);
    nvCmd(0x66);
    nvData(0x21);
    nvCmd(0x76);
    nvData(0x21);
    nvCmd(0x68);
    nvData(0x3A);
    nvCmd(0x78);
    nvData(0x3A);
    nvCmd(0x63);
    nvData(0x07);
    nvCmd(0x73);
    nvData(0x07);
    nvCmd(0x64);
    nvData(0x05);
    nvCmd(0x74);
    nvData(0x05);
    nvCmd(0x65);
    nvData(0x02);
    nvCmd(0x75);
    nvData(0x02);
    nvCmd(0x67);
    nvData(0x23);
    nvCmd(0x77);
    nvData(0x23);
    nvCmd(0x69);
    nvData(0x08);
    nvCmd(0x79);
    nvData(0x08);
    nvCmd(0x6a);
    nvData(0x13);
    nvCmd(0x7a);
    nvData(0x13);
    nvCmd(0x6b);
    nvData(0x13);
    nvCmd(0x7b);
    nvData(0x13);
    nvCmd(0x6f);
    nvData(0x00);
    nvCmd(0x7f);
    nvData(0x00);
    nvCmd(0x50);
    nvData(0x00);
    nvCmd(0x52);
    nvData(0xd6);
    nvCmd(0x53);
    nvData(0x08);
    nvCmd(0x54);
    nvData(0x08);
    nvCmd(0x55);
    nvData(0x1e);
    nvCmd(0x56);
    nvData(0x1c);
    nvCmd(0xa0);
    nvData(0x2b);
    nvData(0x24);
    nvData(0x00);
    nvCmd(0xa1);
    nvData(0x87);
    nvCmd(0xa2);
    nvData(0x86);
    nvCmd(0xa5);
    nvData(0x00);
    nvCmd(0xa6);
    nvData(0x00);
    nvCmd(0xa7);
    nvData(0x00);
    nvCmd(0xa8);
    nvData(0x36);
    nvCmd(0xa9);
    nvData(0x7e);
    nvCmd(0xaa);
    nvData(0x7e);
    nvCmd(0xB9);
    nvData(0x85);
    nvCmd(0xBA);
    nvData(0x84);
    nvCmd(0xBB);
    nvData(0x83);
    nvCmd(0xBC);
    nvData(0x82);
    nvCmd(0xBD);
    nvData(0x81);
    nvCmd(0xBE);
    nvData(0x80);
    nvCmd(0xBF);
    nvData(0x01);
    nvCmd(0xC0);
    nvData(0x02);
    nvCmd(0xc1);
    nvData(0x00);
    nvCmd(0xc2);
    nvData(0x00);
    nvCmd(0xc3);
    nvData(0x00);
    nvCmd(0xc4);
    nvData(0x33);
    nvCmd(0xc5);
    nvData(0x7e);
    nvCmd(0xc6);
    nvData(0x7e);
    nvCmd(0xC8);
    nvData(0x33);
    nvData(0x33);
    nvCmd(0xC9);
    nvData(0x68);
    nvCmd(0xCA);
    nvData(0x69);
    nvCmd(0xCB);
    nvData(0x6a);
    nvCmd(0xCC);
    nvData(0x6b);
    nvCmd(0xCD);
    nvData(0x33);
    nvData(0x33);
    nvCmd(0xCE);
    nvData(0x6c);
    nvCmd(0xCF);
    nvData(0x6d);
    nvCmd(0xD0);
    nvData(0x6e);
    nvCmd(0xD1);
    nvData(0x6f);
    nvCmd(0xAB);
    nvData(0x03);
    nvData(0x67);
    nvCmd(0xAC);
    nvData(0x03);
    nvData(0x6b);
    nvCmd(0xAD);
    nvData(0x03);
    nvData(0x68);
    nvCmd(0xAE);
    nvData(0x03);
    nvData(0x6c);
    nvCmd(0xb3);
    nvData(0x00);
    nvCmd(0xb4);
    nvData(0x00);
    nvCmd(0xb5);
    nvData(0x00);
    nvCmd(0xB6);
    nvData(0x32);
    nvCmd(0xB7);
    nvData(0x7e);
    nvCmd(0xB8);
    nvData(0x7e);
    nvCmd(0xe0);
    nvData(0x00);
    nvCmd(0xe1);
    nvData(0x03);
    nvData(0x0f);
    nvCmd(0xe2);
    nvData(0x04);
    nvCmd(0xe3);
    nvData(0x01);
    nvCmd(0xe4);
    nvData(0x0e);
    nvCmd(0xe5);
    nvData(0x01);
    nvCmd(0xe6);
    nvData(0x19);
    nvCmd(0xe7);
    nvData(0x10);
    nvCmd(0xe8);
    nvData(0x10);
    nvCmd(0xea);
    nvData(0x12);
    nvCmd(0xeb);
    nvData(0xd0);
    nvCmd(0xec);
    nvData(0x04);
    nvCmd(0xed);
    nvData(0x07);
    nvCmd(0xee);
    nvData(0x07);
    nvCmd(0xef);
    nvData(0x09);
    nvCmd(0xf0);
    nvData(0xd0);
    nvCmd(0xf1);
    nvData(0x0e);
    nvData(0x17);
    nvCmd(0xf2);
    nvData(0x2c);
    nvData(0x1b);
    nvData(0x0b);
    nvData(0x20);
    nvCmd(0xe9);
    nvData(0x29);
    nvCmd(0xec);
    nvData(0x04);
    nvCmd(0x35);
    nvData(0x00);
    nvCmd(0x44);
    nvData(0x00);
    nvData(0x10);
    nvCmd(0x46);
    nvData(0x10);
    nvCmd(0xff);
    nvData(0x00);
    nvCmd(0x3a);
    nvData(0x05);     // RGB565
    nvCmd(0x11);      // Sleep out
    delay(220);
    nvCmd(0x29);      // Display on
}


// 【函数说明】旋钮 A 相中断：读取 B 相判断方向，将 raw_knob_counter 加一或减一。
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

// 【函数说明】配置显示、旋钮、按键、背光、功放、ADC 等底层资源；创建 284×76 Sprite 并设置中文字体。
void HAL_Init()
{
    tft.init();

    /*
     * 2.79 寸 142×428 新屏不是原来的 ST7789 初始化序列。
     * 这里在 TFT_eSPI 建立 SPI/引脚能力后，补发厂家例程中的 NV3007 初始化命令。
     * 初始化后再设置 rotation=1，把竖屏 142×428 转成横向 428×142 坐标系。
     */
    HAL_NV3007_Init_142x428();
    tft.setRotation(PrescriptConst::DISPLAY_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // ==========================================
    // 【新增】：背光与功放硬件初始化，防止引脚高阻态
    // ==========================================
    pinMode(PIN_BLK, OUTPUT);
    digitalWrite(PIN_BLK, HIGH); // 新 142×428 屏背光高电平点亮

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

// 【函数说明】原子读取并清零旋钮累计步数，把中断层的脉冲转换为 AppManager 每帧可消费的 delta。
int HAL_Get_Knob_Delta(void)
{
    int raw;
    noInterrupts();
    raw = raw_knob_counter;
    int delta = raw / 4;
    if (delta != 0) {
        raw_knob_counter -= delta * 4;
    }
    interrupts();
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
    File file = LittleFS.open(PrescriptConst::STANDBY_IMAGE_BIN, "r");
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

// 【函数说明】把逻辑 Sprite 推送到物理屏幕偏移位置，完成一次终端带鱼屏刷新。
void HAL_Screen_Update()
{
    /*
     * 整屏刷新逻辑 Sprite。
     *
     * 这里实际写到物理屏幕的位置 = UI 几何居中偏移 + NV3007 GRAM 内部偏移。
     * 这样可以在不破坏 UI 布局坐标的情况下，单独修正新屏底部花线/写窗口错位问题。
     */
    textSprite.pushSprite(
        HAL_DisplayRawX(PrescriptConst::UI_PUSH_X),
        HAL_DisplayRawY(PrescriptConst::UI_PUSH_Y)
    );
}

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    if (color == PrescriptConst::UI_ACCENT_SENTINEL)
        color = TFT_CYAN;
    textSprite.drawLine(x0, y0, x1, y1, color);
}
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == PrescriptConst::UI_ACCENT_SENTINEL)
        color = TFT_CYAN;
    textSprite.drawRect(x, y, w, h, color);
}
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    if (color == PrescriptConst::UI_ACCENT_SENTINEL)
        color = TFT_CYAN;
    textSprite.fillRect(x, y, w, h, color);
}
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color)
{
    if (color == PrescriptConst::UI_ACCENT_SENTINEL)
        color = TFT_CYAN;
    textSprite.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color)
{
    textSprite.drawPixel(x, y, color);
}
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h)
{
    /*
     * 局部刷新 Sprite 的某个区域。
     *
     * x/y/w/h 是逻辑 Sprite 内部区域；
     * 真实屏幕目标坐标仍然要叠加 UI 居中偏移和 NV3007 GRAM 偏移，
     * 否则局部刷新页面可能重新在底部留下脏线。
     */
    textSprite.pushSprite(
        HAL_DisplayRawX(x + PrescriptConst::UI_PUSH_X),
        HAL_DisplayRawY(y + PrescriptConst::UI_PUSH_Y),
        x,
        y,
        w,
        h
    );
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
    bool enable_double_click;

public:
    ButtonEngine(uint32_t lp_ms = 800, uint32_t dg_ms = 250, bool enable_dbl = true)
    {
        long_press_ms = lp_ms;
        double_gap_ms = dg_ms;
        enable_double_click = enable_dbl;
    }

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

        // 【事件 1】：按键被按下的瞬间
        if (current_state && !is_pressed)
        {
            is_pressed = true;
            press_time = now;
            long_triggered = false;

            // 🚀 【极速响应核心】：如果是等待双击的状态，第二次按下的瞬间立刻引爆！绝不等待松手！
            if (enable_double_click && wait_double)
            {
                wait_double = false;
                long_triggered = true; // 借用这个标志位，屏蔽掉后续的松手判断
                return BTN_DOUBLE;
            }
        }
        // 【事件 2】：按键被松开的瞬间
        else if (!current_state && is_pressed)
        {
            is_pressed = false;
            uint32_t duration = now - press_time;

            if (!long_triggered && duration > PrescriptConst::BUTTON_DEBOUNCE_MS) // 20ms 防抖
            {
                if (enable_double_click)
                {
                    wait_double = true;
                    release_time = now;
                }
                else
                {
                    return BTN_SHORT; // 不开双击，松手瞬间极速开火
                }
            }
        }
        // 【事件 3】：按键持续按压中
        else if (current_state && is_pressed)
        {
            if (!long_triggered && (now - press_time > long_press_ms))
            {
                long_triggered = true;
                wait_double = false;
                return BTN_LONG; // 时间一到，精准开火
            }
        }
        // 【事件 4】：按键处于空闲状态
        else if (!current_state && !is_pressed)
        {
            // ⏳ 如果开启了双击，只能在这里乖乖等 250ms 超时，才能判定为单击
            if (enable_double_click && wait_double && (now - release_time > double_gap_ms))
            {
                wait_double = false;
                return BTN_SHORT;
            }
        }
        return result;
    }
};

// ==========================================
// 【完美实例化】：各司其职的按键配置
// ==========================================
// 旋钮主按键：关闭双击 (传入 false)，恢复绝对丝滑的零延迟响应！
ButtonEngine engineMainBtn(PrescriptConst::BUTTON_LONG_MS, PrescriptConst::BUTTON_DOUBLE_GAP_MS, false);

// 副按键 (7号引脚)：开启双击 (传入 true)，承担复杂的宏指令调度！
ButtonEngine engineBtn2(PrescriptConst::BUTTON_LONG_MS, PrescriptConst::BUTTON_DOUBLE_GAP_MS, true);

// ==========================================
// 【休眠系统原子化】：将休眠拆解，供 AppStandby 统一调度
// ==========================================
// 【函数说明】关背光、关功放、让 ST7789 进入 sleep，并准备 Light Sleep 前的硬件静默状态。
void HAL_Sleep_Enter_Prepare()
{
    // 1. 熄灭背光与关断功放
    digitalWrite(PIN_BLK, LOW);  // 新屏背光低电平关闭
    gpio_hold_en((gpio_num_t)PIN_BLK);
    digitalWrite(PIN_AUDIO_SD, LOW);
    gpio_hold_en((gpio_num_t)PIN_AUDIO_SD);
    gpio_deep_sleep_hold_en();

    // 2. 屏幕驱动 IC 内部挂起
    tft.writecommand(0x10);
}

// 【函数说明】配置主按键唤醒源并进入 esp_light_sleep_start，返回时说明用户已经按键唤醒。
void HAL_Sleep_Start()
{
    // 3. 真正的浅睡眠触发
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);
    esp_light_sleep_start();
}

// 【函数说明】唤醒后恢复屏幕、背光、功放、音频、震动和 NFC，并等待主按键释放。
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
    digitalWrite(PIN_BLK, HIGH); // 新屏背光高电平点亮，画面瞬间浮现

    // 5. 唤醒外设
    SysHaptic_Wakeup();
    SysAudio_Wakeup();
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

void HAL_Screen_ShowChineseLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    u8f.setForegroundColor(color);
    u8f.setCursor(x, y + 12);
    u8f.print(str);
}
void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    textSprite.setTextColor(color, TFT_BLACK);
    textSprite.setTextSize(1);
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }
uint16_t HAL_Get_Screen_Width(void) { return PrescriptConst::UI_SCREEN_WIDTH; }
uint16_t HAL_Get_Screen_Height(void) { return PrescriptConst::UI_SCREEN_HEIGHT; }
