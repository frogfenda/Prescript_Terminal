#include "hal.h"
#include "xfont.h"    // 引入你的 tftziku 库
#include "my_image.h" // 引入你的待机立绘图片

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 

XFont* myFont; // 【全局中文字库指针】

// === EC11 旋钮变量 ===
volatile int knob_counter = 0;
volatile uint8_t last_A = 1;

// 【中断服务函数】：利用硬件中断极速捕获旋钮动作
IRAM_ATTR void ISR_Knob_Turn() {
    uint8_t current_A = digitalRead(PIN_KNOB_A);
    uint8_t current_B = digitalRead(PIN_KNOB_B);
    
    if (last_A == 1 && current_A == 0) {
        if (current_B == 0) knob_counter++; // 顺时针（右转）
        else knob_counter--;                // 逆时针（左转）
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
    digitalWrite(PIN_BUZZER, HIGH); // 有源蜂鸣器默认拉高闭嘴

    // === 初始化旋钮并挂载中断 ===
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);

    // === 初始化中文软字库 ===
    myFont = new XFont(false); 
    myFont->setSprite(&textSprite); // 把 1-bit 画布的控制权交给它！
    myFont->initZhiku("/x.font");   // 挂载你存在 LittleFS 里的字库文件
}

// 获取旋钮变化量，并在读取后清零
int Get_Knob_Delta(void) {
    int delta = knob_counter;
    knob_counter = 0; 
    return delta;
}

bool Is_Key_Pressed() {
    return digitalRead(PIN_BTN) == LOW;
}

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
    // 画出你的专属图片 (确保你已经弄好了 my_image_array)
    tft.pushImage(0, 0, 240, 240, my_image_array); 
}

void Screen_ShowTextLine(uint8_t x, uint8_t y, const char* str) {
    textSprite.setTextColor(1, 0); 
    textSprite.setTextSize(2); 
    textSprite.setCursor(x, y);
    textSprite.print(str);
}

// 【终极武器】：画中文！
void Screen_ShowChineseLine(uint8_t x, uint8_t y, const char* str) {
    // 调用 tftziku 的 Ex 极速显示方法。
    // 因为画布是 1-bit，所以 fontColor 填 1，backColor 填 0 (透明)
    myFont->DrawChineseEx(x, y, String(str), 1, 0); 
}

void Screen_Scroll_Up(uint8_t scroll_pixels) {
    textSprite.scroll(0, -scroll_pixels); 
}

void Screen_Update() {
    textSprite.pushSprite(0, 32); 
}