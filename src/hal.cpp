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
    Serial.println("===> HAL_Init: TFT init");
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    Serial.println("===> HAL_Init: Creating Sprite");
    textSprite.setColorDepth(1); 
    void* ptr = textSprite.createSprite(240, 208); 
    if (ptr == NULL) {
        Serial.println("!!! FATAL ERROR: Sprite 创建失败 (内存不足或配置错误) !!!");
    } else {
        Serial.println("===> HAL_Init: Sprite Created OK");
    }
    
    textSprite.fillSprite(0); 
    textSprite.setBitmapColor(TFT_CYAN, TFT_BLACK); 

    Serial.println("===> HAL_Init: Setting Pins");
    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH); 
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);

    Serial.println("===> HAL_Init: U8g2 Begin");
    u8f.begin(textSprite);       
    u8f.setFontMode(1);          
    u8f.setFontDirection(0);     
    u8f.setForegroundColor(1);   
    u8f.setBackgroundColor(0);   
    
    Serial.println("===> HAL_Init: U8g2 Set Font");
    u8f.setFont(u8g2_font_wqy16_t_gb2312);
    
    Serial.println("===> HAL_Init: ALL SUCCESS");
}

int HAL_Get_Knob_Delta(void) { int delta = knob_counter; knob_counter = 0; return delta; }
bool HAL_Is_Key_Pressed() { return digitalRead(PIN_BTN) == LOW; }

void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    digitalWrite(PIN_BUZZER, LOW); delay(duration_ms); digitalWrite(PIN_BUZZER, HIGH);
}
void HAL_Buzzer_Random_Glitch() {
    digitalWrite(PIN_BUZZER, LOW); delay(random(2) + 1); digitalWrite(PIN_BUZZER, HIGH);
}
void HAL_Screen_Clear() { tft.fillScreen(TFT_BLACK); textSprite.fillSprite(0); }
void HAL_Screen_DrawHeader() {
    tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(2); tft.setCursor(10, 8); tft.print("[ PRESCRIPT ]");
}
void HAL_Screen_DrawStandbyImage() {
    Serial.println("===> DRAWING STANDBY IMAGE");
    tft.setSwapBytes(true); tft.pushImage(0, 0, 240, 240, my_image_array); 
}
void HAL_Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(1, 0); textSprite.setTextSize(2); textSprite.setCursor(x, y); textSprite.print(str);
}
void HAL_Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str) {
    u8f.setCursor(x, y + 16); u8f.print(str);
}
void HAL_Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }
void HAL_Screen_Update() { textSprite.pushSprite(0, 32); }
void HAL_Draw_Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) { textSprite.drawLine(x0, y0, x1, y1, color); }
void HAL_Draw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { textSprite.drawRect(x, y, w, h, color); }
void HAL_Fill_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) { textSprite.fillRect(x, y, w, h, color); }
void HAL_Fill_Triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) { textSprite.fillTriangle(x0, y0, x1, y1, x2, y2, color); }
void HAL_Sprite_Clear() { textSprite.fillSprite(0); }