// 文件：src/hal.cpp
#include "hal.h"
#include "my_image.h" 
#include <U8g2_for_TFT_eSPI.h> 

TFT_eSPI tft = TFT_eSPI(); 
TFT_eSprite textSprite = TFT_eSprite(&tft); 
U8g2_for_TFT_eSPI u8f;        

// 文件：src/hal.cpp
// 文件：src/hal.cpp (仅替换这一部分)
// 文件：src/hal.cpp

// 【升级】：改用更精确的底层累加器
volatile int raw_knob_counter = 0;

// 【终极方案】：全波段状态机中断（彻底无视任何速度的物理抖动）
IRAM_ATTR void ISR_Knob_Turn() {
    static uint8_t old_AB = 3; // 初始状态假设均为高电平
    
    // 核心数学滤波器：这个数组涵盖了A和B所有可能的跳变组合
    // 正转+1，反转-1，无效跳变和物理抖动统统为 0！
    static const int8_t enc_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    
    uint8_t A = digitalRead(PIN_KNOB_A);
    uint8_t B = digitalRead(PIN_KNOB_B);
    
    old_AB <<= 2;                   // 将旧状态左移两位
    old_AB |= ((A << 1) | B);       // 拼接当前的新状态，组成 4 bit 索引
    
    // 查表并直接进行数学累加，不加任何 delay！
    raw_knob_counter += enc_states[(old_AB & 0x0f)];
}
void HAL_Init() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    textSprite.setColorDepth(16); 
    // 【解耦 1】：直接调用我们新写的尺寸 API，不再写死 240
    uint16_t sw = HAL_Get_Screen_Width();
    uint16_t sh = HAL_Get_Screen_Height();
    void* ptr = textSprite.createSprite(sw, sh); 
    if (ptr == NULL) Serial.println("!!! Sprite 内存不足 !!!");
    
    textSprite.fillSprite(TFT_BLACK); 

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
// 【更新】：将底层的 4 次状态跳变，换算为 1 次 UI 移动
int HAL_Get_Knob_Delta(void) { 
    int delta = raw_knob_counter / 4;
    if (delta != 0) {
        // 提取出有效的移动格数后，只扣除相应的数值，绝不丢失极高速转动时的余数
        raw_knob_counter -= delta * 4; 
    }
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
    // 【解耦 2】：贴图时也使用动态的宽高参数
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
// 【新增实现】：返回当前画布的大小。以后换屏幕，只需改这里的数字和 tft.createSprite() 即可！
uint16_t HAL_Get_Screen_Width(void) { return 240; }
uint16_t HAL_Get_Screen_Height(void) { return 240; }