#pragma once
#include <Arduino.h>

// Centralized system constants for Prescript Terminal.
// Keep pin layout, UI geometry, persistent limits and protocol identifiers here,
// so HAL/SYS/APP layers do not silently diverge.
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
// Display and UI geometry
// -----------------------------------------------------------------------------
constexpr uint16_t UI_SCREEN_WIDTH  = 284;
constexpr uint16_t UI_SCREEN_HEIGHT = 76;   // Keep 76 unless the physical 78px panel is confirmed usable.
constexpr int16_t UI_PUSH_X = 18;
constexpr int16_t UI_PUSH_Y = 82;

constexpr uint8_t UI_HEADER_HEIGHT = 38;
constexpr uint8_t UI_MARGIN_LEFT   = 20;
constexpr uint8_t UI_MARGIN_RIGHT  = 20;
constexpr uint8_t UI_TEXT_Y_TOP    = 16;
constexpr uint8_t UI_TIME_SAFE_PAD = 28;
constexpr uint8_t UI_FRAME_MS      = 16;

// Legacy drawing wrappers treat color == 1 as the default accent color.
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

constexpr uint8_t MAX_NAV_STACK       = 5;
constexpr uint8_t MAX_BG_APPS         = 10;
constexpr uint8_t MAX_EVENT_SUBSCRIBERS = 24;
constexpr uint8_t MAX_POMODORO_PRESETS = 5;
constexpr uint8_t MAX_COIN_PRESETS    = 10;
constexpr uint8_t MAX_ALARMS          = 10;
constexpr uint8_t MAX_SCHEDULES       = 15;
constexpr uint8_t MAX_CHAR_CHAINS     = 8;
constexpr uint8_t MAX_BLE_QUEUE       = 8;

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
