#include "hal.h"
#include "xfont.h"    
#include "my_image.h" 

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 

XFont* myFont; 

volatile int knob_counter = 0;
volatile uint8_t last_A = 1;

// 【UI引擎系统语言】：现在默认切回中文！
static SystemLang_t current_lang = LANG_ZH; 

void Set_Language(SystemLang_t lang) { current_lang = lang; }
SystemLang_t Get_Language(void) { return current_lang; }

// 【双语字典】
const char* UI_DICT_TITLE[2] = {
    "MAIN MENU", 
    "系统主菜单"
};

const char* UI_DICT_MENU[2][4] = {
    { "EXECUTE PRESCRIPT", "NETWORK SYNC", "ALARM SETTINGS", "SYSTEM STATUS" }, 
    { "执行都市指令", "网络同步授时", "系统闹钟设置", "设备运行状态" }                 
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

    // 正式解封中文引擎！
    myFont = new XFont(true); 
    myFont->setSprite(&textSprite); 
    myFont->initZhiku("/x.font");   
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
    if(myFont) myFont->DrawChineseEx(x, y, String(str), 1, 0); 
}

void Screen_DrawMenu(int current_selection) {
    Screen_Clear();

    textSprite.setTextColor(1, 0);
    textSprite.setTextSize(2); 
    
    // 渲染标题
    if (current_lang == LANG_EN) {
        textSprite.setCursor(66, 10); 
        textSprite.print(UI_DICT_TITLE[LANG_EN]);
    } else {
        Screen_ShowChineseLine(72, 10, UI_DICT_TITLE[LANG_ZH]);
    }
    
    textSprite.drawLine(20, 32, 220, 32, 1);

    int start_y = 50;
    int spacing = 35;

    // 渲染循环列表
    for (int i = 0; i < 4; i++) {
        if (i == current_selection) {
            textSprite.fillRect(15, start_y + i * spacing - 4, 210, 26, 1);
            if (current_lang == LANG_EN) {
                textSprite.setTextColor(0, 1); 
                textSprite.setCursor(45, start_y + i * spacing);
                textSprite.print(UI_DICT_MENU[LANG_EN][i]);
            } else {
                if(myFont) myFont->DrawChineseEx(45, start_y + i * spacing, String(UI_DICT_MENU[LANG_ZH][i]), 0, 1); 
            }
            textSprite.fillTriangle(25, start_y + i * spacing,
                                    25, start_y + i * spacing + 14,
                                    32, start_y + i * spacing + 7, 0);
        } else {
            if (current_lang == LANG_EN) {
                textSprite.setTextColor(1, 0); 
                textSprite.setCursor(45, start_y + i * spacing);
                textSprite.print(UI_DICT_MENU[LANG_EN][i]);
            } else {
                if(myFont) myFont->DrawChineseEx(45, start_y + i * spacing, String(UI_DICT_MENU[LANG_ZH][i]), 1, 0); 
            }
        }
    }
    Screen_Update();
}

void Screen_Scroll_Up(uint8_t scroll_pixels) { textSprite.scroll(0, -scroll_pixels); }
void Screen_Update() { textSprite.pushSprite(0, 32); }