// 文件：src/hal.cpp
#include "hal.h"
#include "my_image.h" 
#include <U8g2_for_TFT_eSPI.h> 

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 
U8g2_for_TFT_eSPI u8f;        

volatile int knob_counter = 0;
volatile uint8_t last_A = 1;

IRAM_ATTR void ISR_Knob_Turn() {
    static uint32_t last_isr_time = 0;
    uint32_t current_time = millis();
    if (current_time - last_isr_time < 5) return; 
    uint8_t current_A = digitalRead(PIN_KNOB_A);
    uint8_t current_B = digitalRead(PIN_KNOB_B);
    if (last_A == 1 && current_A == 0) {
        if (current_B == 0) knob_counter--; 
        else knob_counter++;                
        last_isr_time = current_time; 
    }
    last_A = current_A;
}

void HAL_Init() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // 【核心升级】：从 1-bit 升级到 16-bit 真彩色画布！
    textSprite.setColorDepth(16); 
    void* ptr = textSprite.createSprite(240, 240); 
    if (ptr == NULL) Serial.println("!!! Sprite 内存不足 !!!");
    
    textSprite.fillSprite(TFT_BLACK); 

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH); 
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);

    u8f.begin(textSprite);       
    u8f.setFontMode(1);          
    u8f.setFontDirection(0);     
    u8f.setBackgroundColor(TFT_BLACK);   
    u8f.setFont(u8g2_font_wqy16_t_gb2312);
}

int HAL_Get_Knob_Delta(void) { int delta = knob_counter; knob_counter = 0; return delta; }
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
    textSprite.setTextSize(2); 
    textSprite.setCursor(10, 8); 
    textSprite.print("[ PRESCRIPT ]");
}
void HAL_Screen_DrawStandbyImage() {
    tft.setSwapBytes(true); tft.pushImage(0, 0, 240, 240, my_image_array); 
}

void HAL_Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(TFT_CYAN, TFT_BLACK); 
    textSprite.setTextSize(2); textSprite.setCursor(x, y); textSprite.print(str);
}
void HAL_Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str) {
    u8f.setForegroundColor(TFT_CYAN);
    u8f.setCursor(x, y + 16); u8f.print(str);
}

// 【新增实现】：获取字符串的像素宽度
int HAL_Get_Text_Width(const char* str) {
    return u8f.getUTF8Width(str);
}

// 【新增实现】：景深渐变淡出渲染
void HAL_Screen_ShowChineseLine_Faded(uint8_t x, uint8_t y, const char* str, float distance) {
    // 根据距离计算亮度 (距离越远，亮度越低)
    float intensity = 1.0f - (distance * 0.40f); 
    if (intensity < 0.15f) intensity = 0.15f; // 保底亮度，不至于完全隐形

    // 动态生成暗青色 (Cyan 是 RGB: 0, 255, 255)
    uint8_t g = (uint8_t)(255.0f * intensity);
    uint8_t b = (uint8_t)(255.0f * intensity);
    uint16_t faded_color = tft.color565(0, g, b);

    u8f.setForegroundColor(faded_color);
    u8f.setCursor(x, y + 16); 
    u8f.print(str);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }
void HAL_Screen_Update() { textSprite.pushSprite(0, 0); }

// 【兼容老代码】：截获 color=1，自动转换为 TFT_CYAN
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
void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }