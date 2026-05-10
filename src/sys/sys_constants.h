#pragma once
#include <Arduino.h>

/*
 * 终端全局常量表。
 *
 * 这一版是 142×428 新屏分支的“全屏基线”：
 * - 不再保留 284×76 兼容画布；
 * - HAL_Get_Screen_Width()/Height() 直接返回新屏横向可视区域 428×142；
 * - UI 层后续都以 428×142 为唯一逻辑画布继续重排；
 * - NV3007 控制器内部 RAM 偏移单独保留在 DISPLAY_RAM_OFFSET_*，不和 UI 坐标混用。
 */
namespace PrescriptConst {

// -----------------------------------------------------------------------------
// Hardware pins
// -----------------------------------------------------------------------------
constexpr int PIN_KNOB_A    = 5;
constexpr int PIN_KNOB_B    = 4;
constexpr int PIN_BTN_MAIN  = 6;
constexpr int PIN_BTN_SIDE  = 7;

constexpr int PIN_I2S_BCLK  = 18;
constexpr int PIN_I2S_LRC   = 13;
constexpr int PIN_I2S_DOUT  = 17;
constexpr int PIN_AUDIO_SD  = 48;
constexpr int PIN_BACKLIGHT = 40;

constexpr int PIN_BAT_ADC   = 8;
constexpr int PIN_CHRG      = 16;

constexpr int PIN_I2C_SDA   = 41;
constexpr int PIN_I2C_SCL   = 42;
constexpr uint8_t DRV2605_ADDR = 0x5A;

constexpr int PIN_NFC_SCK   = 1;
constexpr int PIN_NFC_MISO  = 2;
constexpr int PIN_NFC_MOSI  = 47;
constexpr int PIN_NFC_SS    = 15;
constexpr int PIN_NFC_RESET = 21;

// -----------------------------------------------------------------------------
// Display profile: 2.79 inch 142×428 NV3007/NV3006A1 strip screen
// -----------------------------------------------------------------------------
// 面板原生可视区为竖向 142×428。
constexpr uint16_t DISPLAY_NATIVE_VISIBLE_W = 142;
constexpr uint16_t DISPLAY_NATIVE_VISIBLE_H = 428;

// 根据厂家例程：可视 column = 12 ~ 153，因此控制器 native RAM 宽度至少为 156。
// PlatformIO/TFT_eSPI 中需要设置 TFT_WIDTH=156, TFT_HEIGHT=428，避免写入 offset 后底部出现花线。
constexpr uint16_t DISPLAY_NATIVE_RAM_W = 156;
constexpr uint16_t DISPLAY_NATIVE_RAM_H = 428;

// 设备横向使用，TFT_eSPI rotation=1 后的可视逻辑方向为 428×142。
constexpr uint8_t  DISPLAY_ROTATION = 1;
constexpr uint16_t DISPLAY_VISIBLE_WIDTH  = 428;
constexpr uint16_t DISPLAY_VISIBLE_HEIGHT = 142;

// 新屏分支不再保留旧 284×76 兼容画布，UI 的唯一逻辑画布就是整块可视区域。
constexpr uint16_t UI_SCREEN_WIDTH  = DISPLAY_VISIBLE_WIDTH;
constexpr uint16_t UI_SCREEN_HEIGHT = DISPLAY_VISIBLE_HEIGHT;

// UI 画布已经和可视区域等大，所以不需要额外居中偏移。
constexpr int16_t UI_PUSH_X = 0;
constexpr int16_t UI_PUSH_Y = 0;

// NV3007/NV3006A1 内部 GRAM 可视区偏移。
// 厂家例程在原生竖屏坐标下 column +12；横向 rotation=1 后表现为写屏 Y 方向 +12。
// 这个偏移只在 HAL pushSprite 写到物理屏幕时叠加，APP/UI 层完全不用感知。
constexpr int16_t DISPLAY_RAM_OFFSET_X = 0;
constexpr int16_t DISPLAY_RAM_OFFSET_Y = 14;

// 背光极性：新屏为高电平亮、低电平灭。
constexpr uint8_t BACKLIGHT_ON_LEVEL  = HIGH;
constexpr uint8_t BACKLIGHT_OFF_LEVEL = LOW;

// -----------------------------------------------------------------------------
// Legacy UI metrics kept for existing APP code.
// 后续 UITheme 响应式重排时会逐步减少对这些固定值的依赖。
// -----------------------------------------------------------------------------
constexpr uint8_t UI_HEADER_HEIGHT = 38;
constexpr uint8_t UI_MARGIN_LEFT   = 20;
constexpr uint8_t UI_MARGIN_RIGHT  = 20;
constexpr uint8_t UI_TEXT_Y_TOP    = 16;
constexpr uint8_t UI_TIME_SAFE_PAD = 28;
constexpr uint8_t UI_FRAME_MS      = 16;

// 旧绘图包装约定：color == 1 表示默认青色强调色。
constexpr uint16_t UI_ACCENT_SENTINEL = 1;

// -----------------------------------------------------------------------------
// Runtime timings and capacities
// -----------------------------------------------------------------------------
constexpr uint8_t  CPU_RUNTIME_MHZ          = 80;
constexpr uint32_t DEFAULT_IDLE_SLEEP_MS    = 30000UL;
constexpr uint32_t NEVER_SLEEP_MS           = 0xFFFFFFFFUL;
constexpr uint32_t BUTTON_LONG_MS           = 800UL;
constexpr uint32_t BUTTON_DOUBLE_GAP_MS     = 250UL;
constexpr uint32_t BUTTON_DEBOUNCE_MS       = 20UL;

constexpr uint8_t MAX_NAV_STACK         = 5;
constexpr uint8_t MAX_BG_APPS           = 10;
constexpr uint8_t MAX_EVENT_SUBSCRIBERS = 24;
constexpr uint8_t MAX_POMODORO_PRESETS  = 5;
constexpr uint8_t MAX_COIN_PRESETS      = 10;
constexpr uint8_t MAX_ALARMS            = 10;
constexpr uint8_t MAX_SCHEDULES         = 15;
constexpr uint8_t MAX_CHAR_CHAINS       = 8;
constexpr uint8_t MAX_BLE_QUEUE         = 8;

// -----------------------------------------------------------------------------
// Files and BLE protocol identifiers
// -----------------------------------------------------------------------------
constexpr const char* CONFIG_FILE       = "/assets/config.json";
constexpr const char* STANDBY_IMAGE_BIN = "/assets/standby.bin";

constexpr const char* BLE_DEVICE_NAME   = "Terminal_01";
constexpr const char* BLE_SERVICE_UUID  = "0000DEAD-0000-1000-8000-00805F9B34FB";
constexpr const char* BLE_CHAR_UUID     = "0000BEEF-0000-1000-8000-00805F9B34FB";
constexpr const char* NETWORK_SYNC_URL  = "http://index.dimension-404.cloud/api/schedule/sync";

}
