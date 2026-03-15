// 文件：src/hal/hal.cpp
#include "hal.h"
#include <LittleFS.h>           // <--- 引入文件系统！绝不再用 my_image.h！
#include <U8g2_for_TFT_eSPI.h> 
#include "esp_sleep.h"

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 
U8g2_for_TFT_eSPI u8f;        

volatile int raw_knob_counter = 0;

IRAM_ATTR void ISR_Knob_Turn() {
    static uint8_t old_AB = 3; 
    static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t A = digitalRead(PIN_KNOB_A);
    uint8_t B = digitalRead(PIN_KNOB_B);
    old_AB <<= 2;                   
    old_AB |= ((A << 1) | B);       
    raw_knob_counter += enc_states[(old_AB & 0x0f)];
}

void HAL_Init() {
    tft.init();
    tft.setRotation(1); 
    tft.fillScreen(TFT_BLACK);

    textSprite.setColorDepth(16); 
    uint16_t sw = HAL_Get_Screen_Width();
    uint16_t sh = HAL_Get_Screen_Height();
    void* ptr = textSprite.createSprite(sw, sh); 
    if (ptr == NULL) Serial.println("!!! Sprite 内存不足 !!!");
    
    textSprite.fillSprite(TFT_BLACK); 
    textSprite.setTextWrap(false); 

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH); 
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_B), ISR_Knob_Turn, CHANGE);

    u8f.begin(textSprite);       
    u8f.setFontMode(1);          
    u8f.setFontDirection(0);     
    u8f.setBackgroundColor(TFT_BLACK);   
    u8f.setFont(u8g2_font_wqy12_t_gb2312);
}

int HAL_Get_Knob_Delta(void) { 
    int delta = raw_knob_counter / 4;
    if (delta != 0) raw_knob_counter -= delta * 4; 
    return delta; 
}
bool HAL_Is_Key_Pressed() { return digitalRead(PIN_BTN) == LOW; }

void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    digitalWrite(PIN_BUZZER, LOW); delay(duration_ms); digitalWrite(PIN_BUZZER, HIGH);
}
void HAL_Buzzer_Random_Glitch() {
    digitalWrite(PIN_BUZZER, LOW); delay(random(2) + 1); digitalWrite(PIN_BUZZER, HIGH);
}
void HAL_Screen_Clear() { tft.fillScreen(TFT_BLACK); textSprite.fillSprite(TFT_BLACK); }

void HAL_Screen_DrawHeader() {
    textSprite.setTextColor(TFT_RED, TFT_BLACK); 
    textSprite.setTextSize(1); 
    textSprite.setCursor(10, 8); 
    textSprite.print("[ PRESCRIPT ]");
}

// ========================================================
// 【终极进化】：从硬盘安全读取，并画入精灵图缓存，避免任何闪烁和覆盖问题！
// ========================================================
void HAL_Screen_DrawStandbyImage() {
    // 1. 先把整个背景刷成纯黑，以防万一
    textSprite.fillSprite(TFT_BLACK);

    // 2. 尝试打开硬盘中的图像文件
    File file = LittleFS.open("/assets/standby.bin", "r");
    if (!file) {
        // 如果文件不存在，画一个红框并提示，绝对不会黑屏！
        textSprite.drawRect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_RED);
        textSprite.setTextColor(TFT_RED, TFT_BLACK);
        textSprite.drawString("ERR: NO standby.bin", 10, 10);
        return; 
    }

    // 3. 【降维打击】：直接获取 Sprite 底层的显存物理指针
    uint16_t* sprite_ptr = (uint16_t*)textSprite.getPointer();
    
    if (sprite_ptr != nullptr) {
        // 暴力流式灌入：一次性将 43168 字节吸入显存！
        size_t bytes_to_read = HAL_Get_Screen_Width() * HAL_Get_Screen_Height() * 2;
        file.read((uint8_t*)sprite_ptr, bytes_to_read);
        
        // 【色彩反转补丁】：
        // 如果你烧录后发现画面出现了，但是颜色诡异（比如人脸变成蓝色阿凡达），
        // 说明转换图片时高低字节反了。请【取消下面这段代码的注释】即可完美修复：
        /*
        for (int i = 0; i < (HAL_Get_Screen_Width() * HAL_Get_Screen_Height()); i++) {
            sprite_ptr[i] = (sprite_ptr[i] >> 8) | (sprite_ptr[i] << 8);
        }
        */
    }
    file.close();
}

void HAL_Screen_ShowTextLine(int32_t x, int32_t y, const char* str) {
    textSprite.setTextColor(TFT_CYAN, TFT_BLACK); 
    textSprite.setTextSize(1); 
    textSprite.setCursor(x, y); 
    textSprite.print(str);
}

void HAL_Screen_ShowChineseLine(int32_t x, int32_t y, const char* str) {
    u8f.setForegroundColor(TFT_CYAN);
    u8f.setCursor(x, y + 12); 
    u8f.print(str);
}

int HAL_Get_Text_Width(const char* str) { return u8f.getUTF8Width(str); }

void HAL_Screen_ShowChineseLine_Faded(int32_t x, int32_t y, const char* str, float distance) {
    HAL_Screen_ShowChineseLine_Faded_Color(x, y, str, distance, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine_Faded_Color(int32_t x, int32_t y, const char* str, float distance, uint16_t base_color) {
    float intensity = 1.0f - (distance * 0.40f); 
    if (intensity < 0.15f) intensity = 0.15f; 

    uint8_t r8 = ((base_color >> 11) & 0x1F) * 255 / 31;
    uint8_t g8 = ((base_color >> 5)  & 0x3F) * 255 / 63;
    uint8_t b8 =  (base_color        & 0x1F) * 255 / 31;

    uint8_t final_r = (uint8_t)(r8 * intensity);
    uint8_t final_g = (uint8_t)(g8 * intensity);
    uint8_t final_b = (uint8_t)(b8 * intensity);

    uint16_t faded_color = tft.color565(final_r, final_g, final_b);

    u8f.setForegroundColor(faded_color);
    u8f.setCursor(x, y + 12); 
    u8f.print(str);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }

void HAL_Screen_Update() { 
    textSprite.pushSprite(18, 82); 
}

void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) { 
    if(color == 1) color = TFT_CYAN; textSprite.drawLine(x0, y0, x1, y1, color); 
}
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { 
    if(color == 1) color = TFT_CYAN; textSprite.drawRect(x, y, w, h, color); 
}
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { 
    if(color == 1) color = TFT_CYAN; textSprite.fillRect(x, y, w, h, color); 
}
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) { 
    if(color == 1) color = TFT_CYAN; textSprite.fillTriangle(x0, y0, x1, y1, x2, y2, color); 
}
void HAL_Draw_Pixel(int32_t x, int32_t y, uint16_t color) {
    textSprite.drawPixel(x, y, color); 
}
void HAL_Screen_Update_Area(int32_t x, int32_t y, int32_t w, int32_t h) {
    // 【核心修复】：补偿物理屏幕的 (18, 82) 硬件偏移！
    textSprite.pushSprite(x + 18, y + 82, x, y, w, h);
}
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data) {
    // 1. 开启高低字节翻转，修复块传输时的“反色/花屏”问题
    textSprite.setSwapBytes(true); 
    
    // 2. 将硬币数据块极速拍到画布上
    textSprite.pushImage(x, y, w, h, data);
    
    // 3. 画完立刻关闭翻转，防止影响到系统里其他正常文字的颜色！
    textSprite.setSwapBytes(false); 
}
// ==========================================
// 【系统休眠模块】
// ==========================================
void HAL_Sleep_Enter() {
    // 1. 发送 0x10 命令给 ST7789 驱动芯片，让屏幕面板进入睡眠模式（可省 10~15mA）

    delay(120); // 必须等待面板放电
    
    // 2. 核心：设定唯一的唤醒源 —— 旋钮按键（PIN_BTN），并在低电平 (0) 时触发
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);
    
    // 3. 彻底暂停 CPU 运行，进入浅度睡眠
    esp_light_sleep_start();
}

void HAL_Sleep_Exit() {
    // 发送 0x11 命令，唤醒 ST7789 屏幕面板
    tft.writecommand(0x11); 
    delay(120);
}
void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }
uint16_t HAL_Get_Screen_Width(void) { return 284; }
uint16_t HAL_Get_Screen_Height(void) { return 76; }