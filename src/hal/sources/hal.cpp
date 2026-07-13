/*
【模块职责】硬件抽象实现。负责 NV3007 QSPI 长条屏 + U8g2 内存画布显示、旋钮 A/B 相中断计数、两个按键的短按/长按/双击识别，以及通过 BSP 调度背光、功放、屏幕休眠等硬件动作。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/hal/hal.cpp
#include "hal/hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h>
#include "esp_sleep.h"
#include "sys/sys_config.h" // 【新增】：引入全局配置
#include "sys/sys_haptic.h"
#include "sys/sys_audio.h"
#include "sys/sys_nfc.h"
#include "ui/ui_font_config.h"
#include "bsp/bsp_pins.h"
#include "bsp/bsp_display_nv3007.h"
#include "bsp/bsp_power.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite textSprite = TFT_eSprite(&tft);
U8g2_for_TFT_eSPI u8f;

volatile int raw_knob_counter = 0;

/*
 * 将逻辑 UI 坐标转换为 NV3007 QSPI 驱动的横屏逻辑坐标。
 *
 * UI_PUSH_X / UI_PUSH_Y 表示逻辑 Sprite 在新横屏 428×142 可视区域中的位置；当前全屏适配阶段为 0,0；
 * DISPLAY_RAM_OFFSET_X / DISPLAY_RAM_OFFSET_Y 表示横屏逻辑画布在物理面板中的偏移。
 *
 * 这两个偏移必须分开：
 * - 想调整 UI 在可视区中的位置，改 UI_PUSH_X / UI_PUSH_Y；
 * - 想修正 NV3007 物理面板上的画布位置，改 DISPLAY_RAM_OFFSET_*。
 */
static inline int16_t HAL_DisplayRawX(int16_t logical_x)
{
    return logical_x + PrescriptConst::DISPLAY_RAM_OFFSET_X;
}

static inline int16_t HAL_DisplayRawY(int16_t logical_y)
{
    return logical_y + PrescriptConst::DISPLAY_RAM_OFFSET_Y;
}

// NV3007/NV3006A1 QSPI 初始化和刷新已下放到 BSP::DisplayNv3007，HAL 只保留绘图与输入抽象。

// 【函数说明】旋钮 A 相中断：读取 B 相判断方向，将 raw_knob_counter 加一或减一。
IRAM_ATTR void ISR_Knob_Turn()
{
    static uint8_t old_AB = 3;
    static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t A = digitalRead(BSP::Pins::KNOB_A);
    uint8_t B = digitalRead(BSP::Pins::KNOB_B);
    old_AB <<= 2;
    old_AB |= ((A << 1) | B);
    raw_knob_counter += enc_states[(old_AB & 0x0f)];
}

// 【函数说明】配置显示、旋钮、按键等 HAL 资源，并调用 BSP 初始化电源轨；创建 428×142 全屏 Sprite 并设置中文字体。
void HAL_Init()
{
    /*
     * TFT_eSPI/TFT_eSprite 这里只作为内存画布和颜色工具使用。
     * 物理屏幕总线由 BSP::DisplayNv3007 的 QSPI 驱动接管，避免旧 SPI 面板驱动和 NV3007 时序冲突。
     */
    bool displayOk = BSP::DisplayNv3007::Begin();
    if (!displayOk)
        BSP::DisplayNv3007::PrintDiagnostics();

    BSP::Power::BeginRails();

    textSprite.setColorDepth(16);
    uint16_t sw = HAL_Get_Screen_Width();
    uint16_t sh = HAL_Get_Screen_Height();
    void *ptr = textSprite.createSprite(sw, sh);
    if (ptr == NULL)
    {
        Serial.printf("[显示] Sprite 创建失败：需要约 %lu 字节，当前画布=%ux%u。\n",
                      (unsigned long)sw * sh * 2UL, sw, sh);
    }
    else
    {
        Serial.printf("[显示] Sprite 创建成功：画布=%ux%u，约 %lu 字节。\n",
                      sw, sh, (unsigned long)sw * sh * 2UL);
    }

    textSprite.fillSprite(TFT_BLACK);
    textSprite.setTextWrap(false);

    pinMode(BSP::Pins::BTN_MAIN, INPUT_PULLUP);
    pinMode(BSP::Pins::KNOB_A, INPUT_PULLUP);
    pinMode(BSP::Pins::KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BSP::Pins::KNOB_A), ISR_Knob_Turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BSP::Pins::KNOB_B), ISR_Knob_Turn, CHANGE);
    HAL_Btn2_Init();
    u8f.begin(textSprite);
    u8f.setFontMode(1);
    u8f.setFontDirection(0);
    u8f.setBackgroundColor(TFT_BLACK);
    // 默认字体使用正文角色。具体字体和字号在 ui_font_config.h 中配置。
    u8f.setFont(UIFontConfig::Body().font);
}

// 【函数说明】原子读取并清零旋钮累计步数，把中断层的脉冲转换为 AppManager 每帧可消费的 delta。
int HAL_Get_Knob_Delta(void)
{
    int raw;
    noInterrupts();
    raw = raw_knob_counter;
    int delta = raw / 4;
    if (delta != 0)
    {
        raw_knob_counter -= delta * 4;
    }
    interrupts();
    return delta;
}
bool HAL_Is_Key_Pressed() { return digitalRead(BSP::Pins::BTN_MAIN) == LOW; }

void HAL_Screen_Clear()
{
    BSP::DisplayNv3007::FillScreen(TFT_BLACK);
    textSprite.fillSprite(TFT_BLACK);
}

void HAL_Screen_DrawHeader()
{
    textSprite.setTextColor(TFT_RED, TFT_BLACK);
    textSprite.setTextSize(1);
    textSprite.setCursor(10, 8);
    textSprite.print("[ PRESCRIPT ]");
}

// 【函数说明】绘制待机图。
// 新屏横屏模式下，standby.bin 必须匹配 HAL_Get_Screen_Width/Height 的 RGB565 原始图。
// 如果仍然使用旧 284×76 待机图，本函数不会强行读取，避免读越界或显示错乱，而是在屏幕上给出尺寸提示。
void HAL_Screen_DrawStandbyImage()
{
    textSprite.fillSprite(TFT_BLACK);

    const size_t expected_bytes = (size_t)HAL_Get_Screen_Width() * HAL_Get_Screen_Height() * 2;
    File file = LittleFS.open(PrescriptConst::STANDBY_IMAGE_BIN, "r");
    if (!file)
        file = LittleFS.open(PrescriptConst::STANDBY_IMAGE_LEGACY_BIN, "r");

    if (!file)
    {
        Serial.println("[显示] 待机图不存在：/common/standby.bin。");
        textSprite.drawRect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_RED);
        textSprite.setTextColor(TFT_RED, TFT_BLACK);
        textSprite.drawString("NO standby.bin", 12, 12);
        char need_buf[40];
        snprintf(need_buf, sizeof(need_buf), "need %ux%u RGB565", HAL_Get_Screen_Width(), HAL_Get_Screen_Height());
        textSprite.drawString(need_buf, 12, 28);
        return;
    }

    if ((size_t)file.size() < expected_bytes)
    {
        Serial.printf("[显示] 待机图尺寸过小：当前 %lu 字节，需要 %lu 字节。请重新导出 428x142 RGB565。\n",
                      (unsigned long)file.size(),
                      (unsigned long)expected_bytes);
        file.close();

        textSprite.drawRect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_ORANGE);
        textSprite.setTextColor(TFT_ORANGE, TFT_BLACK);
        textSprite.drawString("standby.bin size mismatch", 12, 12);
        char need_buf[40];
        snprintf(need_buf, sizeof(need_buf), "need %ux%u RGB565", HAL_Get_Screen_Width(), HAL_Get_Screen_Height());
        textSprite.drawString(need_buf, 12, 28);
        return;
    }

    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (sprite_ptr != nullptr)
    {
        file.read((uint8_t *)sprite_ptr, expected_bytes);
    }
    file.close();
}


/**
 * 根据 HAL 字体角色取得字体配置。
 *
 * 这里是 HAL 字体系统的唯一入口：
 * - 字体数组、baseline、lineHeight 都来自 ui_font_config.h；
 * - 页面不直接接触 u8g2_font_xxx，后续换字体只改配置文件；
 * - 返回值按值传递，避免跨文件静态对象初始化顺序问题。
 */
static UIFontConfig::FontSpec HAL_GetFontSpec(HALFontRole role)
{
    switch (role)
    {
    case HAL_FONT_SMALL:
        return UIFontConfig::Small();
    case HAL_FONT_TITLE:
        return UIFontConfig::Title();
    case HAL_FONT_BODY:
    default:
        return UIFontConfig::Body();
    }
}

/**
 * 设置 U8g2 当前字体角色。
 *
 * U8g2_for_TFT_eSPI 每次绘制前都可以切换字体；这里集中封装，
 * 保证所有中文、英文、数字都走同一套 UTF-8 字体管线，不再混用 TFT_eSPI 默认 6×8 小字。
 */
static void HAL_ApplyFontRole(HALFontRole role)
{
    UIFontConfig::FontSpec spec = HAL_GetFontSpec(role);
    u8f.setFont(spec.font);
}

int HAL_Get_Font_Baseline(HALFontRole role)
{
    return HAL_GetFontSpec(role).baseline;
}

int HAL_Get_Font_Line_Height(HALFontRole role)
{
    return HAL_GetFontSpec(role).lineHeight;
}

int HAL_Get_Text_Width_Font(const char *str, HALFontRole role)
{
    if (!str)
        return 0;

    HAL_ApplyFontRole(role);
    return u8f.getUTF8Width(str);
}

int HAL_Get_Text_Width_Small(const char *str)
{
    return HAL_Get_Text_Width_Font(str, HAL_FONT_SMALL);
}

/**
 * 按指定字体角色绘制一行 UTF-8 文本。
 *
 * 参数约定：
 * - x/y 是文本框左上角；
 * - baseline 由字体角色决定，避免以前 y+12 写死后换字号导致裁切；
 * - color 是 RGB565 颜色；
 * - 字体角色由 UI 层决定，HAL 只负责准确落字。
 */
void HAL_Screen_ShowLine_Font(int32_t x, int32_t y, const char *str, HALFontRole role, uint16_t color)
{
    if (!str)
        return;

    HAL_ApplyFontRole(role);
    u8f.setForegroundColor(color);
    u8f.setCursor(x, y + HAL_Get_Font_Baseline(role));
    u8f.print(str);
}

void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, TFT_CYAN);
}

int HAL_Get_Text_Width(const char *str)
{
    return HAL_Get_Text_Width_Font(str, HAL_FONT_BODY);
}

void HAL_Screen_ShowSmallLine(int32_t x, int32_t y, const char *str)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_SMALL, TFT_CYAN);
}

void HAL_Screen_ShowSmallLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_SMALL, color);
}

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

    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, faded_color);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }

// 【函数说明】把逻辑 Sprite 推送到物理屏幕偏移位置，完成一次终端带鱼屏刷新。
void HAL_Screen_Update()
{
    /*
     * 整屏刷新逻辑 Sprite。
     *
     * 这里实际写到物理屏幕的位置 = UI 几何偏移 + NV3007 横屏逻辑偏移。
     * BSP 会按 DISPLAY_ROTATION 把 428×142 逻辑画布旋转到 168×428 物理面板中。
     */
    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (!sprite_ptr)
        return;

    BSP::DisplayNv3007::PushImageRotated(PrescriptConst::DISPLAY_ROTATION,
                                         HAL_DisplayRawX(PrescriptConst::UI_PUSH_X),
                                         HAL_DisplayRawY(PrescriptConst::UI_PUSH_Y),
                                         HAL_Get_Screen_Width(),
                                         HAL_Get_Screen_Height(),
                                         sprite_ptr,
                                         HAL_Get_Screen_Width(),
                                         true);
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
     * 真实屏幕目标坐标仍然要叠加 UI 偏移和 NV3007 横屏逻辑偏移。
     */
    if (w <= 0 || h <= 0)
        return;

    uint16_t sw = HAL_Get_Screen_Width();
    uint16_t sh = HAL_Get_Screen_Height();
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x >= sw || y >= sh)
        return;
    if (x + w > sw)
        w = sw - x;
    if (y + h > sh)
        h = sh - y;
    if (w <= 0 || h <= 0)
        return;

    uint16_t *sprite_ptr = (uint16_t *)textSprite.getPointer();
    if (!sprite_ptr)
        return;

    BSP::DisplayNv3007::PushImageRotated(PrescriptConst::DISPLAY_ROTATION,
                                         HAL_DisplayRawX(x + PrescriptConst::UI_PUSH_X),
                                         HAL_DisplayRawY(y + PrescriptConst::UI_PUSH_Y),
                                         (uint16_t)w,
                                         (uint16_t)h,
                                         sprite_ptr + (size_t)y * sw + x,
                                         sw,
                                         true);
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
    ButtonEngine(uint32_t lp_ms = 800, uint32_t dg_ms = 250, bool enable_dbl = true, uint32_t db_ms = 35)
    {
        long_press_ms = lp_ms;
        double_gap_ms = dg_ms;
        enable_double_click = enable_dbl;
        debounce_ms = db_ms;
    }

    void reset()
    {
        is_pressed = false;
        wait_double = false;
        long_triggered = false;
        raw_state = false;
        stable_state = false;
        raw_change_time = millis();
    }

    BtnEvent update(bool raw_pressed)
    {
        uint32_t now = millis();
        BtnEvent result = BTN_NONE;

        if (raw_pressed != raw_state)
        {
            raw_state = raw_pressed;
            raw_change_time = now;
        }

        bool current_state = stable_state;
        if (raw_state != stable_state && (now - raw_change_time >= debounce_ms))
        {
            stable_state = raw_state;
            current_state = stable_state;
        }

        // 【事件 1】：按键被按下的瞬间
        if (current_state && !is_pressed)
        {
            is_pressed = true;
            press_time = now;
            long_triggered = false;

            // 极速响应核心：如果是等待双击的状态，第二次按下的瞬间立刻触发。
            if (enable_double_click && wait_double)
            {
                wait_double = false;
                long_triggered = true;
                return BTN_DOUBLE;
            }
        }
        // 【事件 2】：按键被松开的瞬间
        else if (!current_state && is_pressed)
        {
            is_pressed = false;
            uint32_t duration = now - press_time;

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
        // 【事件 3】：按键持续按压中
        else if (current_state && is_pressed)
        {
            if (!long_triggered && (now - press_time > long_press_ms))
            {
                long_triggered = true;
                wait_double = false;
                return BTN_LONG;
            }
        }
        // 【事件 4】：按键处于空闲状态
        else if (!current_state && !is_pressed)
        {
            if (enable_double_click && wait_double && (now - release_time > double_gap_ms))
            {
                wait_double = false;
                return BTN_SHORT;
            }
        }
        return result;
    }
};

ButtonEngine engineMainBtn(PrescriptConst::BUTTON_LONG_MS,
                           PrescriptConst::BUTTON_DOUBLE_GAP_MS,
                           false,
                           PrescriptConst::BUTTON_MAIN_DEBOUNCE_MS);
ButtonEngine engineBtn2(PrescriptConst::BUTTON_LONG_MS,
                        PrescriptConst::BUTTON_DOUBLE_GAP_MS,
                        true,
                        PrescriptConst::BUTTON_SIDE_DEBOUNCE_MS);

// ==========================================
// 【休眠系统原子化】：将休眠拆解，供 AppStandby 统一调度
// ==========================================
// 【函数说明】关背光、关功放、让显示控制器进入 sleep，并准备 Light Sleep 前的硬件静默状态。
void HAL_Sleep_Enter_Prepare()
{
    // 1. 熄灭背光与关断功放
    BSP::Power::SetBacklight(false);
    BSP::Power::SetAudioAmp(false);
    BSP::Power::HoldBacklightAndAudio();

    // 2. 屏幕驱动 IC 内部挂起
    BSP::DisplayNv3007::Sleep();
}

// 【函数说明】配置主按键唤醒源并进入 esp_light_sleep_start，返回时说明用户已经按键唤醒。
void HAL_Sleep_Start()
{
    // 3. 真正的浅睡眠触发
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BSP::Pins::BTN_MAIN, 0);
    esp_light_sleep_start();
}

// 【函数说明】唤醒后恢复屏幕、背光、功放、音频、震动和 NFC，并等待主按键释放。
void HAL_Sleep_Wakeup_Post()
{
    // 1. 先解除 hold 并明确保持背光/功放关闭，避免 wake 过程露出空白帧。
    BSP::Power::ReleaseBacklightAndAudio();
    BSP::Power::SetBacklight(false);
    BSP::Power::SetAudioAmp(false);

    // 2. 唤醒屏幕驱动 IC。DisplayOn 会等新画面写入后再发送。
    BSP::DisplayNv3007::Wakeup();

    // 3. 趁着背光还没点亮，先把待机画面刷进屏幕 GRAM。
    // 这样开灯瞬间就能看到完整画面，而不是黑屏或随机显存噪点。
    HAL_Screen_DrawStandbyImage();
    HAL_Screen_Update();
    BSP::DisplayNv3007::DisplayOn();

    // 4. display-on 后留一点稳定时间，再打开背光。
    delay(20);

    // 5. 画面稳了，再点亮背光与功放。
    BSP::Power::SetAudioAmp(true);
    BSP::Power::SetBacklight(true); // 新屏背光高电平点亮，画面瞬间浮现

    // 6. 唤醒外设
    SysHaptic_Wakeup();
    SysAudio_Wakeup();
    SysNfc_Wakeup();

    // 7. 防误触：吞掉唤醒时的那次点击
    while (digitalRead(BSP::Pins::BTN_MAIN) == LOW || digitalRead(BSP::Pins::BTN_SIDE) == LOW)
    {
        delay(10);
    }
    engineMainBtn.reset();
    engineBtn2.reset();
}

void HAL_Btn2_Init()
{
    pinMode(BSP::Pins::BTN_SIDE, INPUT_PULLUP);
}

BtnEvent HAL_Get_Btn_Main_Event()
{
    extern bool HAL_Is_Key_Pressed();
    return engineMainBtn.update(HAL_Is_Key_Pressed());
}

BtnEvent HAL_Get_Btn2_Event()
{
    // 读取引脚并喂给状态机引擎
    BtnEvent evt = engineBtn2.update(digitalRead(BSP::Pins::BTN_SIDE) == LOW);

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
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, color);
}

void HAL_Screen_ShowTextLine_Color(int32_t x, int32_t y, const char *str, uint16_t color)
{
    HAL_Screen_ShowLine_Font(x, y, str, HAL_FONT_BODY, color);
}

void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }
uint16_t HAL_Get_Screen_Width(void) { return PrescriptConst::UI_SCREEN_WIDTH; }
uint16_t HAL_Get_Screen_Height(void) { return PrescriptConst::UI_SCREEN_HEIGHT; }


