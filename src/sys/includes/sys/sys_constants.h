#pragma once
#include <Arduino.h>
#include "bsp/bsp_pins.h"

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
    constexpr int PIN_KNOB_A = BSP::Pins::KNOB_A;
    constexpr int PIN_KNOB_B = BSP::Pins::KNOB_B;
    constexpr int PIN_BTN_MAIN = BSP::Pins::BTN_MAIN;
    constexpr int PIN_BTN_SIDE = BSP::Pins::BTN_SIDE;

    constexpr int PIN_I2S_BCLK = BSP::Pins::I2S_BCLK;
    constexpr int PIN_I2S_LRC = BSP::Pins::I2S_LRC;
    constexpr int PIN_I2S_DOUT = BSP::Pins::I2S_DOUT;
    constexpr int PIN_AUDIO_SD = BSP::Pins::AUDIO_SD;
    constexpr int PIN_BACKLIGHT = BSP::Pins::BACKLIGHT;

    constexpr int PIN_BAT_ADC = BSP::Pins::BAT_ADC;
    constexpr int PIN_CHRG = BSP::Pins::CHRG;

    constexpr int PIN_I2C_SDA = BSP::Pins::I2C_SDA;
    constexpr int PIN_I2C_SCL = BSP::Pins::I2C_SCL;
    constexpr uint8_t DRV2605_ADDR = 0x5A;

    constexpr int PIN_NFC_SCK = BSP::Pins::NFC_SCK;
    constexpr int PIN_NFC_MISO = BSP::Pins::NFC_MISO;
    constexpr int PIN_NFC_MOSI = BSP::Pins::NFC_MOSI;
    constexpr int PIN_NFC_SS = BSP::Pins::NFC_SS;
    constexpr int PIN_NFC_RESET = BSP::Pins::NFC_RESET;

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
    constexpr uint8_t MAX_EVENT_SUBSCRIBERS = 28;
    constexpr uint8_t MAX_POMODORO_PRESETS = 5;
    constexpr uint8_t MAX_COIN_PRESETS = 10;
    constexpr uint8_t MAX_COIN_COUNT = 18;
    constexpr uint8_t MAX_ALARMS = 10;
    constexpr uint8_t MAX_SCHEDULES = 15;
    constexpr uint8_t MAX_CHAR_CHAINS = 8;
    constexpr uint8_t MAX_BLE_QUEUE = 8;
    constexpr uint8_t MAX_PRESCRIPT_TARGETS = 12;
    constexpr uint8_t MAX_PRESCRIPT_TARGET_LEN = 24;

    // -----------------------------------------------------------------------------
    // Files and BLE protocol identifiers
    // -----------------------------------------------------------------------------
    constexpr const char *CONFIG_COMMON_FILE = "/common/config.json";
    constexpr const char *CONFIG_COMMON_DEFAULT_FILE = "/common/config_common.json";
    constexpr const char *CONFIG_ZH_FILE = "/zh/config.json";
    constexpr const char *CONFIG_EN_FILE = "/en/config.json";
    constexpr const char *CONFIG_LEGACY_FILE = "/assets/config.json";
    constexpr const char *STANDBY_IMAGE_BIN = "/common/standby.bin";
    constexpr const char *STANDBY_IMAGE_LEGACY_BIN = "/assets/standby.bin";
    constexpr const char *AUDIO_PROCEDURE_WAV = "/common/procedure.wav";
    constexpr const char *AUDIO_FINAL_WAV = "/common/final.wav";
    constexpr const char *AUDIO_AHAB_WAV = "/common/Ahab.wav";
    constexpr const char *COIN_ASSET_DIR = "/common/coins/";
    constexpr const char *COIN_ASSET_LEGACY_DIR = "/assets/coins/";

    constexpr const char *BLE_DEVICE_NAME = "Terminal_01";
    constexpr const char *BLE_SERVICE_UUID = "0000DEAD-0000-1000-8000-00805F9B34FB";
    constexpr const char *BLE_CHAR_UUID = "0000BEEF-0000-1000-8000-00805F9B34FB";
    constexpr const char *NETWORK_SYNC_URL = "http://index.dimension-404.cloud/api/schedule/sync";

}

