#include "hal.h"

TFT_eSPI tft = TFT_eSPI(); 
// 【数学补完】：208 像素高，刚好容纳完美的 13 行文字！
TFT_eSprite textSprite = TFT_eSprite(&tft); 

void HAL_Init() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    // tft.invertDisplay(true); 

    textSprite.setColorDepth(8);
    // 画布尺寸：240x208
    textSprite.createSprite(240, 208); 
    textSprite.fillSprite(TFT_BLACK);

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); 
}

bool Is_Key_Pressed() {
    return digitalRead(PIN_BTN) == LOW;
}

void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    tone(PIN_BUZZER, freq, duration_ms);
    delay(duration_ms);
    noTone(PIN_BUZZER);
}

void Buzzer_Random_Glitch() {
    tone(PIN_BUZZER, (unsigned int)(500 + random(2000)), 10);
}

void Screen_Clear() {
    tft.fillScreen(TFT_BLACK);
    textSprite.fillSprite(TFT_BLACK);
}

void Screen_DrawHeader() {
    tft.setTextColor(TFT_RED, TFT_BLACK); 
    tft.setTextSize(2); 
    tft.setCursor(10, 8); // 【修改】：往上挪一点，给下面留足空间
    tft.print("[ PRESCRIPT ]");
}

void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(0xFDA0, TFT_BLACK); 
    textSprite.setTextSize(2); 
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

void Screen_Scroll_Up(uint8_t scroll_pixels) {
    textSprite.scroll(0, -scroll_pixels); 
}

void Screen_Update() {
    // 【修改】：从 y=32 开始推屏，底下刚好 208 像素
    textSprite.pushSprite(0, 32); 
}