#pragma once
#include <Arduino.h>

/*
【模块职责】系统常量中心。

这里集中保存硬件引脚、屏幕几何、运行容量和协议标识，避免 HAL / SYS / APP 层各自写一套魔法数字。
当前新屏分支已经不保留 284×76 兼容画布，逻辑 UI 直接使用 428×142 全屏画布。
*/
namespace PrescriptConst
{

    // -----------------------------------------------------------------------------
    // Hardware pins
    // -----------------------------------------------------------------------------
    constexpr int PIN_KNOB_A = 5;
    constexpr int PIN_KNOB_B = 4;
    constexpr int PIN_BTN_MAIN = 6;
    constexpr int PIN_BTN_SIDE = 7;

    constexpr int PIN_I2S_BCLK = 18;
    constexpr int PIN_I2S_LRC = 13;
    constexpr int PIN_I2S_DOUT = 17;
    constexpr int PIN_AUDIO_SD = 48;
    constexpr int PIN_BACKLIGHT = 40;

    constexpr int PIN_BAT_ADC = 8;
    constexpr int PIN_CHRG = 16;

    constexpr int PIN_I2C_SDA = 41;
    constexpr int PIN_I2C_SCL = 42;
    constexpr uint8_t DRV2605_ADDR = 0x5A;

    constexpr int PIN_NFC_SCK = 1;
    constexpr int PIN_NFC_MISO = 2;
    constexpr int PIN_NFC_MOSI = 47;
    constexpr int PIN_NFC_SS = 15;
    constexpr int PIN_NFC_RESET = 21;

    // -----------------------------------------------------------------------------
    // Display and UI geometry
    // -----------------------------------------------------------------------------
    /*
     * 新屏幕说明：
     * - 面板可视区：142×428；设备横向使用后为 428×142；
     * - TFT_eSPI 仍借用 ST7789 驱动通道，但通过 HAL 补发 NV3007 初始化序列；
     * - platformio.ini 中 TFT_WIDTH 必须设为 156、TFT_HEIGHT 设为 428；
     * - 156 不是可视宽度，而是控制器原生方向的 RAM 宽度，用来覆盖 column offset 后的可视范围；
     * - DISPLAY_RAM_OFFSET_Y=14 是实测后的横屏写入偏移，用来消除底部花线并保持画面居中。
     */
    constexpr uint16_t DISPLAY_VISIBLE_WIDTH = 428;
    constexpr uint16_t DISPLAY_VISIBLE_HEIGHT = 142;
    constexpr uint8_t DISPLAY_ROTATION = 1;

    constexpr int16_t DISPLAY_RAM_OFFSET_X = 0;
    constexpr int16_t DISPLAY_RAM_OFFSET_Y = 14;

    // 逻辑 UI 画布：本分支不保留旧兼容模式，所有页面直接面向 428×142 设计。
    constexpr uint16_t UI_SCREEN_WIDTH = DISPLAY_VISIBLE_WIDTH;
    constexpr uint16_t UI_SCREEN_HEIGHT = DISPLAY_VISIBLE_HEIGHT;
    constexpr int16_t UI_PUSH_X = 0;
    constexpr int16_t UI_PUSH_Y = 0;

    // 这些旧常量仍作为 AppBase / 老页面的兼容入口，数值按新屏重新给出。
    constexpr uint8_t UI_HEADER_HEIGHT = 46;
    constexpr uint8_t UI_MARGIN_LEFT = 24;
    constexpr uint8_t UI_MARGIN_RIGHT = 24;
    constexpr uint8_t UI_TEXT_Y_TOP = 18;
    constexpr uint8_t UI_TIME_SAFE_PAD = 34;
    constexpr uint8_t UI_FRAME_MS = 16;

    // Legacy drawing wrappers treat color == 1 as the default accent color.
    constexpr uint16_t UI_ACCENT_SENTINEL = 1;

    // -----------------------------------------------------------------------------
    // Runtime timings and capacities
    // -----------------------------------------------------------------------------
    constexpr uint8_t CPU_RUNTIME_MHZ = 160;
    constexpr uint32_t DEFAULT_IDLE_SLEEP_MS = 30000UL;
    constexpr uint32_t NEVER_SLEEP_MS = 0xFFFFFFFFUL;
    constexpr uint32_t BUTTON_LONG_MS = 800UL;
    constexpr uint32_t BUTTON_DOUBLE_GAP_MS = 250UL;
    constexpr uint32_t BUTTON_DEBOUNCE_MS = 20UL;

    constexpr uint8_t MAX_NAV_STACK = 5;
    constexpr uint8_t MAX_BG_APPS = 10;
    constexpr uint8_t MAX_EVENT_SUBSCRIBERS = 24;
    constexpr uint8_t MAX_POMODORO_PRESETS = 5;
    constexpr uint8_t MAX_COIN_PRESETS = 10;
    constexpr uint8_t MAX_COIN_COUNT = 18;
    constexpr uint8_t MAX_ALARMS = 10;
    constexpr uint8_t MAX_SCHEDULES = 15;
    constexpr uint8_t MAX_CHAR_CHAINS = 8;
    constexpr uint8_t MAX_BLE_QUEUE = 8;

    // -----------------------------------------------------------------------------
    // Files and BLE protocol identifiers
    // -----------------------------------------------------------------------------
    constexpr const char *CONFIG_FILE = "/assets/config.json";
    constexpr const char *STANDBY_IMAGE_BIN = "/assets/standby.bin";

    constexpr const char *BLE_DEVICE_NAME = "Terminal_01";
    constexpr const char *BLE_SERVICE_UUID = "0000DEAD-0000-1000-8000-00805F9B34FB";
    constexpr const char *BLE_CHAR_UUID = "0000BEEF-0000-1000-8000-00805F9B34FB";
    constexpr const char *NETWORK_SYNC_URL = "http://index.dimension-404.cloud/api/schedule/sync";

}
