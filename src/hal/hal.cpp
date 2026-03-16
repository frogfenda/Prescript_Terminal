// 文件：src/hal/hal.cpp
#include "hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h> 
#include "esp_sleep.h"
#include <driver/i2s.h> // 【核心新增】：ESP32 I2S 底层驱动

#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==========================================
// 【音频守护线程全局变量】
// ==========================================
static const uint8_t* async_audio_data = nullptr;
static uint32_t async_audio_len = 0;
static TaskHandle_t audioTaskHandle = NULL;

// 【后台异步播放线程】
void audio_bg_task(void *pvParameters) {
    size_t bytes_written;
    while (1) {
        // 如果收到了播放任务
        if (async_audio_data != nullptr && async_audio_len > 0) {
            // 在独立核心中阻塞推流，绝对不会卡住主渲染循环！
            i2s_write(I2S_NUM_0, async_audio_data, async_audio_len, &bytes_written, portMAX_DELAY);
            
            // 播完后清空任务，等待下一次召唤
            async_audio_data = nullptr;
            async_audio_len = 0;
        }
        // 闲置时休眠，让出 CPU 算力
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

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
    pinMode(PIN_KNOB_A, INPUT_PULLUP);
    pinMode(PIN_KNOB_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_A), ISR_Knob_Turn, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_KNOB_B), ISR_Knob_Turn, CHANGE);

    u8f.begin(textSprite);       
    u8f.setFontMode(1);          
    u8f.setFontDirection(0);     
    u8f.setBackgroundColor(TFT_BLACK);   
    u8f.setFont(u8g2_font_wqy12_t_gb2312);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000, // 【降频核心】：16kHz，完美平衡音质与面包板电磁抗干扰能力！
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, 
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,   // 增加缓冲池数量
        .dma_buf_len = 512,
        .use_apll = true,     // 开启硬件锁相环，消灭底噪
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRC,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0); 
    // 【新增】：启动异步音频守护线程 
    // 参数 0 代表将此任务绑定在“核心 0”上运行，彻底避开默认在核心 1 上狂奔的 UI 渲染！
    xTaskCreatePinnedToCore(audio_bg_task, "Audio_Task", 4096, NULL, 1, &audioTaskHandle, 0);
}
int HAL_Get_Knob_Delta(void) { 
    int delta = raw_knob_counter / 4;
    if (delta != 0) raw_knob_counter -= delta * 4; 
    return delta; 
}
bool HAL_Is_Key_Pressed() { return digitalRead(PIN_BTN) == LOW; }


// ==========================================
// 【I2S 纯净数字音频合成器 (50%方波 + 指数级打击衰减)】
// 专为小尺寸喇叭调校，确保高频清脆，低频不破音
// ==========================================
void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    if (freq == 0 || duration_ms == 0) return;
    
    uint32_t sample_rate = 16000; // 与 HAL_Init 中的 16kHz 配置保持一致
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    
    // 音量调节 (0 - 32767)。8000 搭配方波能量已经非常饱满
    int16_t max_volume = 8000; 
    size_t bytes_written;

    // DMA 块传输缓冲池，彻底消灭单步传输导致的断流和浑浊杂音
    const int CHUNK_SAMPLES = 256; 
    int16_t buf[CHUNK_SAMPLES * 2]; // *2 是因为双声道
    int buf_idx = 0;

    float period = (float)sample_rate / freq;

    for (uint32_t i = 0; i < total_samples; i++) {
        
        // 1. 生成 50% 经典方波 (Square Wave)
        // 配合拔高后的宏定义频率，产生极其干脆的电子设备 UI 质感
        float phase = fmod((float)i, period) / period; 
        float wave = (phase < 0.5f) ? 1.0f : -1.0f; 

        // 2. 指数级打击衰减 (Percussive Decay)
        // 模拟实体按键/机械键盘的物理回弹感，瞬间爆发后极速收束，强行抑制喇叭纸盆的杂乱共振
        float linear_envelope = 1.0f - ((float)i / total_samples); 
        float envelope = linear_envelope * linear_envelope; 

        // 3. 计算本帧最终音频振幅
        int16_t sample_val = (int16_t)(wave * max_volume * envelope);
        
        // 4. 填入左右双声道
        buf[buf_idx++] = sample_val; 
        buf[buf_idx++] = sample_val; 

        // 5. 缓冲池满了，批量推入 I2S 底层硬件
        if (buf_idx >= CHUNK_SAMPLES * 2) {
            i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
            buf_idx = 0; 
        }
    }
    
    // 把最后剩下的碎片数据推出去
    if (buf_idx > 0) {
        i2s_write(I2S_NUM_0, buf, buf_idx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
}

// ==========================================
// 【科幻电火花音效合成器 (16kHz 降频适配版)】
// ==========================================
void HAL_Buzzer_Random_Glitch() {
    uint32_t duration_ms = random(10, 25);
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    size_t bytes_written;

    int16_t current_val = 0;
    int hold_counter = 0;
    int16_t max_volume = 8000; 

    const int CHUNK_SAMPLES = 256;
    int16_t buf[CHUNK_SAMPLES * 2];
    int buf_idx = 0;

    for (uint32_t i = 0; i < total_samples; i++) {
        // 降频采样：保持数值一段时间，产生“噼啪”颗粒感，而不是刺耳的嘶嘶雪花声
        if (hold_counter <= 0) {
            current_val = random(-max_volume, max_volume);
            hold_counter = random(3, 12); 
        }
        hold_counter--;

        // 线性衰减
        float envelope = 1.0f - ((float)i / total_samples);
        int16_t sample_val = (int16_t)(current_val * envelope);

        buf[buf_idx++] = sample_val;
        buf[buf_idx++] = sample_val;

        if (buf_idx >= CHUNK_SAMPLES * 2) {
            i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
            buf_idx = 0;
        }
    }
    
    if (buf_idx > 0) {
        i2s_write(I2S_NUM_0, buf, buf_idx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
}

// ==========================================
// 【真实 WAV 音效播放接口 (扩展挂载点)】
// ==========================================
// ==========================================
// 【真实 WAV 音效播放接口 (双核异步 0 延迟版)】
// ==========================================
void HAL_Play_Real_Sound(const uint8_t* audio_data, uint32_t data_length) {
    if (audio_data == nullptr || data_length == 0) return;
    
    // 我们不再这里调用耗时的 i2s_write。
    // 只做一件事：把要播放的内存地址交给守护线程，然后立刻 Return (耗时不到 1 微秒)！
    async_audio_len = 0; // 先清零防止冲突
    async_audio_data = audio_data;
    async_audio_len = data_length;
}

void HAL_Screen_Clear() { tft.fillScreen(TFT_BLACK); textSprite.fillSprite(TFT_BLACK); }

void HAL_Screen_DrawHeader() {
    textSprite.setTextColor(TFT_RED, TFT_BLACK); 
    textSprite.setTextSize(1); 
    textSprite.setCursor(10, 8); 
    textSprite.print("[ PRESCRIPT ]");
}

void HAL_Screen_DrawStandbyImage() {
    textSprite.fillSprite(TFT_BLACK);
    File file = LittleFS.open("/assets/standby.bin", "r");
    if (!file) {
        textSprite.drawRect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_RED);
        textSprite.setTextColor(TFT_RED, TFT_BLACK);
        textSprite.drawString("ERR: NO standby.bin", 10, 10);
        return; 
    }
    uint16_t* sprite_ptr = (uint16_t*)textSprite.getPointer();
    if (sprite_ptr != nullptr) {
        size_t bytes_to_read = HAL_Get_Screen_Width() * HAL_Get_Screen_Height() * 2;
        file.read((uint8_t*)sprite_ptr, bytes_to_read);
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
    textSprite.pushSprite(x + 18, y + 82, x, y, w, h);
}
void HAL_Sprite_PushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data) {
    textSprite.setSwapBytes(true); 
    textSprite.pushImage(x, y, w, h, data);
    textSprite.setSwapBytes(false); 
}

void HAL_Sleep_Enter() {
    delay(120);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);
    esp_light_sleep_start();
}

void HAL_Sleep_Exit() {
    tft.writecommand(0x11); 
    delay(120);
}
void HAL_Sprite_Clear() { textSprite.fillSprite(TFT_BLACK); }
uint16_t HAL_Get_Screen_Width(void) { return 284; }
uint16_t HAL_Get_Screen_Height(void) { return 76; }