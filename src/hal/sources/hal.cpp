/*
【模块职责】硬件抽象实现。负责 NV3007 QSPI 长条屏 + U8g2 内存画布显示、旋钮 A/B 相中断计数、两个按键的短按/长按/双击识别，以及通过 BSP 调度背光、功放、屏幕休眠等硬件动作。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/hal/hal.cpp
#include "hal/hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
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
    // MSC 连接页与更新进度页可能在同一启动周期连续使用 HAL。
    // 初始化必须幂等，避免重复创建 Sprite 或重复注册旋钮中断。
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

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

    /*
     * U8g2_for_TFT_eSPI 的 u8g2_SetFont() 在字体指针变化时会把 is_transparent 重置为 0，
     * 也就是重新启用背景色填充。初始化阶段只调用一次 setFontMode(1) 并不够：页面在
     * BODY/TITLE/SMALL 之间切换后，下一次文字就会在彩色海面上绘制黑色字形矩形。
     * 因此每次应用字体后都恢复透明模式；黑底页面视觉不变，叠加式 UI 则保留原背景像素。
     */
    u8f.setFontMode(1);
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

    /**
     * 【函数说明】把按键引擎复位到当前真实电平，并清除双击/长按历史。
     * 【唤醒场景】如果 raw_pressed=true，说明用于唤醒的按键尚未释放：
     * 引擎会把这次按压标记为已经处理，只等待并吞掉它的释放，不生成短按或长按事件。
     * 这样唤醒恢复函数无需阻塞等待机械按键释放，主循环可以立即恢复运行。
     */
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

/**
 * 【函数说明】把两个实体按键统一恢复为普通轮询输入，并明确清除 Light Sleep 遗留的电平中断类型。
 * 【调用时机】每次 Light Sleep 返回、关闭 GPIO 唤醒源之后调用。
 * 【实现原因】当前 Arduino-ESP32 框架的 pinMode() 会沿用 GPIO 寄存器中原有的 intr_type；
 * gpio_wakeup_enable(..., GPIO_INTR_LOW_LEVEL) 设置过的低电平类型不能只靠再次 pinMode(INPUT_PULLUP) 清除。
 * 如果残留低电平中断配置，唤醒后的普通 digitalRead 轮询可能持续受到该配置干扰，表现为两个按键都不再产生事件。
 * 【返回值】返回 ESP-IDF 的 GPIO 配置结果，调用者必须在失败时打印错误，但不阻塞设备继续运行。
 */
static esp_err_t HAL_RestoreButtonInputs()
{
    gpio_config_t button_config = {};
    button_config.pin_bit_mask = (1ULL << BSP::Pins::BTN_MAIN) |
                                 (1ULL << BSP::Pins::BTN_SIDE);
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&button_config);
}

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

/*
 * 【函数说明】把两个实体按键和可选定时器配置为 Light Sleep 唤醒源，然后进入浅睡眠。
 * 【实现约束】两个按键都是 INPUT_PULLUP、低电平按下，因此统一使用 GPIO_INTR_LOW_LEVEL。
 * 这里使用 ESP-IDF 的 Light Sleep GPIO 唤醒接口，不再使用只能绑定单个 RTC IO 的 EXT0：
 * - 主按键 GPIO21 和侧键 GPIO15 都可以唤醒；
 * - 唤醒后按键仍由普通数字 GPIO 管理，避免主按键留在 RTC IO 状态而无法继续轮询。
 */
HALSleepWakeReason HAL_Sleep_Start(uint64_t timer_wakeup_us)
{
    const gpio_num_t main_button = (gpio_num_t)BSP::Pins::BTN_MAIN;
    const gpio_num_t side_button = (gpio_num_t)BSP::Pins::BTN_SIDE;

    // 1. 休眠前先从确定的普通输入状态开始，避免上一次异常退出遗留 GPIO 中断类型。
    esp_err_t input_config_err = HAL_RestoreButtonInputs();
    if (input_config_err != ESP_OK)
    {
        Serial.printf("[休眠] 休眠前按键 GPIO 配置失败：错误码=%d，本次不进入休眠。\n",
                      (int)input_config_err);
        return HALSleepWakeReason::Error;
    }

    // 2. Light Sleep 的 GPIO 唤醒接口支持多个 RTC/普通数字 GPIO，正好覆盖两个实体按键。
    esp_err_t main_wakeup_err = gpio_wakeup_enable(main_button, GPIO_INTR_LOW_LEVEL);
    esp_err_t side_wakeup_err = gpio_wakeup_enable(side_button, GPIO_INTR_LOW_LEVEL);
    esp_err_t source_err = esp_sleep_enable_gpio_wakeup();
    esp_err_t timer_err = ESP_OK;
    if (timer_wakeup_us > 0)
        timer_err = esp_sleep_enable_timer_wakeup(timer_wakeup_us);

    if (main_wakeup_err != ESP_OK || side_wakeup_err != ESP_OK ||
        source_err != ESP_OK || timer_err != ESP_OK)
    {
        Serial.printf("[休眠-错误] 唤醒源配置失败：主键=%d，侧键=%d，GPIO=%d，定时器=%d。\n",
                      (int)main_wakeup_err,
                      (int)side_wakeup_err,
                      (int)source_err,
                      (int)timer_err);

        // 唤醒源不完整时不能冒险进入无可靠出口的睡眠；清理本轮配置并恢复普通输入。
        gpio_wakeup_disable(main_button);
        gpio_wakeup_disable(side_button);
        if (source_err == ESP_OK)
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        if (timer_wakeup_us > 0 && timer_err == ESP_OK)
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

        esp_err_t cleanup_err = HAL_RestoreButtonInputs();
        if (cleanup_err != ESP_OK)
        {
            Serial.printf("[休眠-错误] 唤醒源清理后 GPIO 恢复失败：错误码=%d。\n",
                          (int)cleanup_err);
        }
        return HALSleepWakeReason::Error;
    }

    // 3. CPU 在这里暂停，直到任一按键被按下或休眠请求被底层拒绝。
    esp_err_t sleep_err = esp_light_sleep_start();
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

    /*
     * 4. 唤醒源只服务本次 Light Sleep，返回后立即解除。
     * gpio_wakeup_disable() 只负责关闭唤醒能力；随后必须用 intr_type=GPIO_INTR_DISABLE
     * 重新配置普通输入，不能调用会保留旧 intr_type 的 Arduino pinMode()。
     */
    gpio_wakeup_disable(main_button);
    gpio_wakeup_disable(side_button);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (timer_wakeup_us > 0)
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

    esp_err_t restore_err = HAL_RestoreButtonInputs();
    if (restore_err != ESP_OK)
    {
        Serial.printf("[休眠] 按键 GPIO 普通输入模式恢复失败：错误码=%d。\n", (int)restore_err);
    }

    if (sleep_err != ESP_OK)
    {
        Serial.printf("[休眠-错误] Light Sleep 未正常执行，错误码=%d。\n", (int)sleep_err);
        return HALSleepWakeReason::Error;
    }

    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER)
        return HALSleepWakeReason::Timer;
    if (wakeup_cause == ESP_SLEEP_WAKEUP_GPIO)
        return HALSleepWakeReason::Button;

    Serial.printf("[休眠-警告] Light Sleep 返回了未处理的唤醒原因：%d。\n", (int)wakeup_cause);
    return HALSleepWakeReason::Error;
}

// 【函数说明】唤醒后恢复屏幕、背光、功放、音频、震动和 NFC，并让按键引擎非阻塞地吞掉唤醒按压。
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

    // 6. 逐项唤醒外设。
    SysHaptic_Wakeup();
    SysAudio_Wakeup();
    SysNfc_Wakeup();

    /*
     * 7. 按当前真实电平重置事件引擎。
     * 旧逻辑会在这里阻塞等待“两键同时稳定释放”，一旦任一 GPIO 没有及时回到高电平，
     * 屏幕虽然已经显示待机图，AppManager 却永远无法继续运行，后续两个按键都会失效。
     * 新逻辑不再等待：仍处于低电平的唤醒按键由 ButtonEngine 在主循环中静默吞掉释放事件，
     * 释放后的下一次完整按压才会正常产生 BTN_SHORT/BTN_LONG/BTN_DOUBLE。
     * 两个 GPIO 的输入、上拉和中断类型已经在 HAL_Sleep_Start() 返回前由
     * HAL_RestoreButtonInputs() 原子恢复，这里不能再用会保留中断类型的 pinMode() 覆盖。
     */
    bool main_still_pressed = digitalRead(BSP::Pins::BTN_MAIN) == LOW;
    bool side_still_pressed = digitalRead(BSP::Pins::BTN_SIDE) == LOW;
    engineMainBtn.reset(main_still_pressed);
    engineBtn2.reset(side_still_pressed);
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
    // 读取侧键真实电平并交给统一按键状态机；正常按键事件不再持续输出串口日志。
    return engineBtn2.update(digitalRead(BSP::Pins::BTN_SIDE) == LOW);
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


