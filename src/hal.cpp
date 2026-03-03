#include "hal.h"
#include "my_image.h" 
#include <U8g2_for_TFT_eSPI.h> 

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 
U8g2_for_TFT_eSPI u8f;        

volatile int knob_counter = 0;
volatile uint8_t last_A = 1;

static SystemLang_t current_lang = LANG_ZH; 

void Set_Language(SystemLang_t lang) { current_lang = lang; }
SystemLang_t Get_Language(void) { return current_lang; }
void Toggle_Language(void) { current_lang = (current_lang == LANG_EN) ? LANG_ZH : LANG_EN; }

// 【全面升级的双语字典】5个选项！
const char* UI_DICT_TITLE[2] = {
    "MAIN CONSOLE", 
    "系统主控制台"
};

const char* UI_DICT_MENU[2][5] = {
    { "EXECUTE PRESCRIPT", "NETWORK TIME SYNC", "SWITCH LANGUAGE", "SLEEP SETTINGS", "RETURN STANDBY" }, 
    // 中文采用极其常用的高频词汇，确保 100% 命中字库
    { "运行都市程序", "同步网络时间", "切换系统语言", "设定休眠时间", "返回待机画面" }                 
};

IRAM_ATTR void ISR_Knob_Turn() {
    uint8_t current_A = digitalRead(PIN_KNOB_A);
    uint8_t current_B = digitalRead(PIN_KNOB_B);
    if (last_A == 1 && current_A == 0) {
        if (current_B == 0) knob_counter++; 
        else knob_counter--;                
    }
    last_A = current_A;
}

void HAL_Init() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    textSprite.setColorDepth(1); 
    textSprite.createSprite(240, 208); 
    textSprite.fillSprite(0); 
    textSprite.setBitmapColor(TFT_CYAN, TFT_BLACK); 

    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH); 

    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);

    u8f.begin(textSprite);       
    u8f.setFontMode(1);          
    u8f.setFontDirection(0);     
    u8f.setForegroundColor(1);   
    u8f.setBackgroundColor(0);   
    // 【升级字库】：换用容量更大的 chinese3，涵盖几乎所有现代常用词汇！
    u8f.setFont(u8g2_font_wqy16_t_chinese3); 
}

int Get_Knob_Delta(void) {
    int delta = knob_counter;
    knob_counter = 0; 
    return delta;
}
bool Is_Key_Pressed() { return digitalRead(PIN_BTN) == LOW; }

void Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    digitalWrite(PIN_BUZZER, LOW);
    delay(duration_ms);
    digitalWrite(PIN_BUZZER, HIGH);
}
void Buzzer_Random_Glitch() {
    digitalWrite(PIN_BUZZER, LOW);
    delay(random(2) + 1);
    digitalWrite(PIN_BUZZER, HIGH);
}

void Screen_Clear() {
    tft.fillScreen(TFT_BLACK);
    textSprite.fillSprite(0); 
}
void Screen_DrawHeader() {
    tft.setTextColor(TFT_RED, TFT_BLACK); 
    tft.setTextSize(2); 
    tft.setCursor(10, 8); 
    tft.print("[ PRESCRIPT ]");
}
void Screen_DrawStandbyImage() {
    tft.setSwapBytes(true); 
    tft.pushImage(0, 0, 240, 240, my_image_array); 
}

void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(1, 0); 
    textSprite.setTextSize(2); 
    textSprite.setCursor(x, y); 
    textSprite.print(str);
}
void Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str) {
    u8f.setCursor(x, y + 16); 
    u8f.print(str);
}

// 【动态多语言菜单渲染器】
void Screen_DrawMenu(int current_selection) {
    Screen_Clear();
    Screen_DrawHeader(); 
    
    // 渲染标题
    u8f.setCursor(70, 32);
    u8f.print(UI_DICT_TITLE[current_lang]);
    textSprite.drawLine(20, 38, 220, 38, 1);

    int start_y = 52;
    int spacing = 30; // 间距调小，完美塞下 5 个选项

    for (int i = 0; i < 5; i++) {
        if (i == current_selection) {
            textSprite.drawRect(10, start_y + i * spacing - 4, 220, 26, 1); 
            
            u8f.setCursor(40, start_y + i * spacing + 14);
            u8f.print(UI_DICT_MENU[current_lang][i]);
            
            textSprite.fillTriangle(20, start_y + i * spacing,
                                    20, start_y + i * spacing + 14,
                                    27, start_y + i * spacing + 7, 1); 
                                    
            textSprite.fillRect(190, start_y + i * spacing + 2, 8, 14, 1);
        } else {
            u8f.setCursor(40, start_y + i * spacing + 14);
            u8f.print(UI_DICT_MENU[current_lang][i]);
        }
    }
    Screen_Update();
}

void Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }
void Screen_Update() { textSprite.pushSprite(0, 32); }