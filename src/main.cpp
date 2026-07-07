#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "bsp/bsp_pins.h"
#include "bsp/bsp_tm6605.h"
#include "sys/sys_config.h"
#include "sys/sys_time.h"
#include "sys/sys_network.h"
#include "sys/sys_auto_push.h"
#include "sys/sys_ble.h"
#include "sys/sys_fs.h"
#include "hal/hal.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"
#include "sys/sys_nfc.h"
#include "sys/sys_specials.h"
#include "sys/sys_power.h"
#include "sys/sys_constants.h"
#include "sys/sys_prescript_target.h"

namespace
{
    constexpr bool TM6605_BOOT_TEST = false;

    uint32_t s_tm6605_last_play_ms = 0;
    uint8_t s_tm6605_effect_index = 0;

    constexpr BSP::Tm6605::Effect TEST_EFFECTS[] = {
        BSP::Tm6605::Effect::SharpClick,
        BSP::Tm6605::Effect::LightBump,
        BSP::Tm6605::Effect::DoubleClick,
        BSP::Tm6605::Effect::MediumAlert,
    };

    const char *EffectName(uint8_t effect)
    {
        switch (effect)
        {
        case BSP::Tm6605::Effect::SharpClick:
            return "SharpClick";
        case BSP::Tm6605::Effect::LightBump:
            return "LightBump";
        case BSP::Tm6605::Effect::DoubleClick:
            return "DoubleClick";
        case BSP::Tm6605::Effect::MediumAlert:
            return "MediumAlert";
        default:
            return "Unknown";
        }
    }

    const char *I2CDeviceName(uint8_t address)
    {
        switch (address)
        {
        case 0x2C:
            return "QMC5883P";
        case BSP::Tm6605::DEFAULT_ADDRESS:
            return "TM6605";
        case 0x51:
            return "PCF8563";
        case 0x6A:
            return "LSM6DSL";
        default:
            return "unknown";
        }
    }

    void ScanI2C()
    {
        uint8_t found = 0;
        Serial.printf("[I2C] scan begin: SCL=%d SDA=%d\n", BSP::Pins::I2C_SCL, BSP::Pins::I2C_SDA);

        for (uint8_t address = 0x08; address <= 0x77; ++address)
        {
            Wire1.beginTransmission(address);
            uint8_t err = Wire1.endTransmission();
            if (err == 0)
            {
                ++found;
                const char *name = I2CDeviceName(address);
                Serial.printf("[I2C] found 0x%02X %s\n", address, name);
            }
            delay(2);
        }

        Serial.printf("[I2C] scan done, found %u device(s).\n", found);
    }

    void PlayNextTm6605Effect(bool force)
    {
        const uint8_t effect = static_cast<uint8_t>(TEST_EFFECTS[s_tm6605_effect_index]);
        const uint16_t duration = BSP::Tm6605::EffectDurationMs(effect);

        Serial.printf("[TM6605] addr=0x%02X command: 0x%02X=0x%02X, 0x%02X=0x%02X (%s, %ums)\n",
                      BSP::Tm6605::Address(),
                      BSP::Tm6605::REG_EFFECT_SELECT,
                      effect,
                      BSP::Tm6605::REG_PLAY_CONTROL,
                      BSP::Tm6605::PLAY_ENABLE,
                      EffectName(effect),
                      duration);

        bool ok = BSP::Tm6605::PlayEffect(effect, force);
        Serial.println(ok ? "[TM6605] play ok." : "[TM6605] play failed/busy.");

        s_tm6605_effect_index = (s_tm6605_effect_index + 1) % (sizeof(TEST_EFFECTS) / sizeof(TEST_EFFECTS[0]));
        s_tm6605_last_play_ms = millis();
    }

    void Tm6605BootTestSetup()
    {
        setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);

        Serial.begin(115200);
        delay(500);
        Serial.println("\n=== TM6605 Boot Test ===");
        Serial.println("[Main] APP startup is disabled in this test build.");

        Wire1.begin(BSP::Pins::I2C_SDA, BSP::Pins::I2C_SCL);
        Wire1.setClock(100000);
        Wire1.setTimeOut(20);

        ScanI2C();

        if (!BSP::Tm6605::Begin(Wire1, BSP::Tm6605::DEFAULT_ADDRESS))
        {
            Serial.println("[TM6605] not ready, commands will not be sent.");
            return;
        }

        Serial.println("[TM6605] ready, sending first effect.");
        PlayNextTm6605Effect(true);
    }

    void Tm6605BootTestLoop()
    {
        if (!BSP::Tm6605::IsReady())
        {
            delay(1000);
            return;
        }

        if (millis() - s_tm6605_last_play_ms >= 2000)
            PlayNextTm6605Effect(false);

        delay(10);
    }
}

void setup()
{
    if (TM6605_BOOT_TEST)
    {
        Tm6605BootTestSetup();
        return;
    }

    setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);

    Serial.begin(115200);
    Serial.println("[Main] System Booting...");

    /*
     * 开机先显式关闭 WiFi。
     * 网络模块后续会通过 Network_RequestBootSync() 延迟触发自动同步，
     * 避免 setup 阶段立刻拉起 WiFi 扫描导致首屏和菜单动画变慢。
     */
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    /*
     * 文件系统和配置必须先初始化。
     * 后面的语言、指令库、特殊指令、网络配置都依赖 sysConfig 和 LittleFS。
     */
    SysFS_Init();
    sysConfig.load();

    /*
     * 先把配置中的语言写入 AppManager。
     * 这样 SysFS_Load_Prescripts() 和 sysSpecials.begin() 能按正确语言加载资源。
     */
    appManager.loadLanguageFromConfig();
    SysFS_Load_Prescripts();
    sysSpecials.begin();

    /*
     * SysTime_Init 只设置时区和清空本次开机的 NTP 记录，不联网。
     * 网络对时由 Network_Init + Network_RequestBootSync 延迟完成。
     */
    SysTime_Init();

    sysAudio.begin();
    sysPower.begin();
    HAL_Init();
    appManager.begin();

    extern void SysRouter_Init();
    SysRouter_Init();
    SysPrescriptTarget_Init();

    sysHaptic.begin();
    SysAutoPush_Init();
    SysBLE_Init();
    sysNfc.begin();

    Network_Init();

    /*
     * 保留“开机自动同步”体验，但延迟 4 秒触发。
     * 同步内容仍然是完整流程：WiFi -> NTP -> 隐秘指令 API。
     * 延迟触发的好处是 UI 先进入 loop，用户不会在无网环境下看到首屏卡住。
     */
    Network_RequestBootSync(4000);
}

void loop()
{
    if (TM6605_BOOT_TEST)
    {
        Tm6605BootTestLoop();
        return;
    }

    /*
     * 网络轻量维护：
     * - 到点触发开机自动同步；
     * - 网络总超时兜底；
     * - 如果用户开启周期校时，到间隔后启动轻量 NTP 校时。
     *
     * 这里不执行 WiFi.begin 或 HTTP，只做状态判断和任务通知。
     */
    Network_Update();

    SysAutoPush_Update();
    appManager.run();
    delay(1);
}
