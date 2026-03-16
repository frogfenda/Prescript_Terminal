// 文件：src/hal/hal.cpp
#include "hal.h"
#include <LittleFS.h>
#include <U8g2_for_TFT_eSPI.h> 
#include "esp_sleep.h"
#include <driver/i2s.h> // 【核心新增】：ESP32 I2S 底层驱动
#include "../sys/sys_config.h" // 【新增】：引入全局配置
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==========================================
// 【音频守护线程全局变量】
// ==========================================
static const uint8_t* async_audio_data = nullptr;
static uint32_t async_audio_len = 0;
static TaskHandle_t audioTaskHandle = NULL;



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

// ==========================================
// 【后台异步播放线程 (带软件实时数字调音台)】
// ==========================================
void audio_bg_task(void *pvParameters) {
    size_t bytes_written;
    while (1) {
        // 如果收到了播放任务
        if (async_audio_data != nullptr && async_audio_len > 0) {
            
            uint8_t vol = sysConfig.volume;
            
            if (vol == 0) {
                // 【静音模式】：直接丢弃播放任务，不推流
            } 
            else if (vol == 100) {
                // 【满音量模式】：不需要算力，直接将内存数据原封不动全速推给 DMA
                i2s_write(I2S_NUM_0, async_audio_data, async_audio_len, &bytes_written, portMAX_DELAY);
            } 
            else {
                // 【调音模式】：启动 CPU 实时解码运算
                int16_t* pcm = (int16_t*)async_audio_data;
                uint32_t total_samples = async_audio_len / 2; // 16-bit 深度，每2个字节是一个采样点
                
                // 开辟小块缓冲池，防止爆内存
                const int CHUNK_SAMPLES = 256;
                int16_t buf[CHUNK_SAMPLES]; 
                uint32_t ptr = 0;
                
                // 应用平方级衰减（符合人耳听觉的真实物理对数曲线）
                float vol_ratio = (float)vol / 100.0f;
                float multiplier = vol_ratio * vol_ratio;
                
               while (ptr < total_samples) {
                    // 【核心魔法：强制打断机制】
                    // 如果外部切了歌（比如突然收到 final.wav 的指令），立刻跳出循环斩断当前波形！
                    if (async_audio_data != (const uint8_t*)pcm) {
                        break; 
                    }

                    int chunk = (total_samples - ptr < CHUNK_SAMPLES) ? (total_samples - ptr) : CHUNK_SAMPLES;
                    for (int i = 0; i < chunk; i++) {
                        buf[i] = (int16_t)(pcm[ptr + i] * multiplier);
                    }
                    i2s_write(I2S_NUM_0, buf, chunk * sizeof(int16_t), &bytes_written, portMAX_DELAY);
                    ptr += chunk;
                }
                
                // 只有当指针没被别人抢走时，才意味着正常播完了，可以清空任务
                if (async_audio_data == (const uint8_t*)pcm) {
                    async_audio_data = nullptr;
                    async_audio_len = 0;
                }
            }
            
            // 播完后清空任务，等待下一次硬币抛掷
            async_audio_data = nullptr;
            async_audio_len = 0;
        }
        
        // 闲置时休眠 10 毫秒，绝不抢占 UI 渲染算力
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
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
// 【I2S 纯净数字音频合成器 (带动态低频保护与曲线音量)】
// ==========================================
void HAL_Buzzer_Play_Tone(uint16_t freq, uint16_t duration_ms) {
    if (freq == 0 || duration_ms == 0 || sysConfig.volume == 0) return;
    
    uint32_t sample_rate = 16000; 
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    
    // 【魔法 1：听觉指数曲线】
    // 将 0-100 的音量转化为 0-12000 的硬件振幅，使用平方算法更符合人耳对响度的感知
    float vol_ratio = (float)sysConfig.volume / 100.0f;
    int16_t max_volume = (int16_t)(12000.0f * vol_ratio * vol_ratio); 

    // 【魔法 2：软件级动态高通滤波 (Dynamic High-Pass)】
    // 如果系统呼叫了低频 (<1500Hz)，强制将其推力降低一半！防止纸盆打底产生机械杂音！
    if (freq < 1500) {
        max_volume = max_volume / 2;
    }

    size_t bytes_written;
    const int CHUNK_SAMPLES = 256; 
    int16_t buf[CHUNK_SAMPLES * 2]; 
    int buf_idx = 0;

    float period = (float)sample_rate / freq;

    for (uint32_t i = 0; i < total_samples; i++) {
        
        float phase = fmod((float)i, period) / period; 
        
        // 【魔法 3：自适应波形切换】
        // 高频用 50% 方波保证清脆响亮；
        // 低频自动切换成 25% 窄脉冲，极大削弱容易引起共振的基频能量，用高频谐波骗过耳朵！
        float duty = (freq < 1500) ? 0.25f : 0.5f;
        float wave = (phase < duty) ? 1.0f : -1.0f; 

        // 指数级打击衰减
        float linear_envelope = 1.0f - ((float)i / total_samples); 
        float envelope = linear_envelope * linear_envelope; 

        int16_t sample_val = (int16_t)(wave * max_volume * envelope);
        
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
// 【科幻打字机 / 机械电火花音效 (彻底告别雪花白噪)】
// ==========================================
// ==========================================
// 【科幻打字机音效 (极速降频扫频 Chirp 完全体)】
// 彻底消灭直流爆音，打造极其精密的玻璃/金属齿轮咬合感
// ==========================================
void HAL_Buzzer_Random_Glitch() {
    if (sysConfig.volume == 0) return;

    uint32_t sample_rate = 16000;
    // 每次敲击的时间极其短促 (3~5毫秒)，绝不拖泥带水
    uint32_t duration_ms = random(3, 6); 
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    
    float vol_ratio = (float)sysConfig.volume / 100.0f;
    // 扫频能量非常集中，音量不需要极大就能听得很清晰
    int16_t max_volume = (int16_t)(10000.0f * vol_ratio * vol_ratio); 

    size_t bytes_written;
    // 5毫秒最多 80 个采样点，开 256 绝对够用，一次性推流 0 延迟
    int16_t buf[256]; 
    int buf_idx = 0;

    // 【核心声学魔法：扫频参数】
    // 每次敲击的起始频率稍微有一点点随机波动，模拟机械键盘不同按键的细微差异
    float start_freq = (float)random(3500, 4500); 
    float end_freq = 800.0f; // 瞬间坠落到中低频收尾
    float current_phase = 0.0f;

    for (uint32_t i = 0; i < total_samples; i++) {
        float progress = (float)i / total_samples;
        
        // 1. 频率随时间呈线性极速下坠 (Chirp)
        float current_freq = start_freq - (start_freq - end_freq) * progress;
        
        // 2. 累加相位 (扫频波形的唯一正确算法)
        current_phase += current_freq / sample_rate;
        if (current_phase > 1.0f) current_phase -= 1.0f;

        // 3. 使用三角波 (Triangle Wave) 
        // 相比方波的刺耳，三角波带有类似玻璃和金属弹片的圆润光泽感
        float wave = 4.0f * fabs(current_phase - 0.5f) - 1.0f; 

        // 4. 指数极速衰减，强行刹住喇叭纸盆
        float envelope = (1.0f - progress) * (1.0f - progress);

        int16_t sample_val = (int16_t)(wave * max_volume * envelope);
        
        // 填入左右双声道
        buf[buf_idx++] = sample_val; 
        buf[buf_idx++] = sample_val; 
    }
    
    // 将整个音频块瞬间砸给 DMA，没有任何碎片断流
    if (buf_idx > 0) {
        i2s_write(I2S_NUM_0, buf, buf_idx * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
}
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