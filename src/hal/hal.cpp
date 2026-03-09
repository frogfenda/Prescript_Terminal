// 文件：src/hal/hal.cpp
#include "hal.h"
#include "my_image.h" 
#include <U8g2_for_TFT_eSPI.h> 

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
    
    // 【核心修复！！！】：强制关闭 TFT_eSPI 的幽灵自动换行！
    // 宁可让超出的文字被屏幕边缘切掉一半，也绝不让它去下一行捣乱覆盖！
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
    u8f.setFont(u8g2_font_wqy16_t_gb2312);
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
    textSprite.setTextSize(2); 
    textSprite.setCursor(10, 8); 
    textSprite.print("[ PRESCRIPT ]");
}
void HAL_Screen_DrawStandbyImage() {
    tft.setSwapBytes(true); 
    tft.pushImage(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), my_image_array); 
}

void HAL_Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(TFT_CYAN, TFT_BLACK); 
    textSprite.setTextSize(2); textSprite.setCursor(x, y); textSprite.print(str);
}
void HAL_Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str) {
    u8f.setForegroundColor(TFT_CYAN);
    u8f.setCursor(x, y + 16); u8f.print(str);
}

int HAL_Get_Text_Width(const char* str) { return u8f.getUTF8Width(str); }

void HAL_Screen_ShowChineseLine_Faded(uint8_t x, uint8_t y, const char* str, float distance) {
    HAL_Screen_ShowChineseLine_Faded_Color(x, y, str, distance, TFT_CYAN);
}

void HAL_Screen_ShowChineseLine_Faded_Color(uint8_t x, uint8_t y, const char* str, float distance, uint16_t base_color) {
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
    u8f.setCursor(x, y + 16); 
    u8f.print(str);
}

void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }
void HAL_Screen_Update() { textSprite.pushSprite(0, 0); }

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
uint16_t HAL_Get_Screen_Width(void) { return 284; }
uint16_t HAL_Get_Screen_Height(void) { return 76; }