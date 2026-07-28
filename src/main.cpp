#include <Arduino.h>
#include <WiFi.h>
#include "sys/sys_config.h"
#include "sys/sys_time.h"
#include "sys/sys_network.h"
#include "sys/sys_auto_push.h"
#include "sys/sys_ble.h"
#include "sys/sys_fs.h"
#include "sys/sys_boot_test.h"
#include "hal/hal.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"
#include "sys/sys_nfc.h"
#include "sys/sys_power.h"
#include "sys/sys_constants.h"
#include "sys/sys_prescript_target.h"
#include "sys/sys_motion.h"
#include "sys/sys_gesture.h"
#include "sys/sys_usb_mode.h"
#include "sys/sys_usb_session.h"
#include "sys/sys_fat_update.h"

void setup()
{
    setCpuFrequencyMhz(PrescriptConst::CPU_RUNTIME_MHZ);

    const bool bootTestEnabled = SysBootTest::Enabled();
    SysUsbSession::BeginAndHandleBootMode(bootTestEnabled, SysBootTest::AllowsMscBoot());

    if (bootTestEnabled)
    {
        SysBootTest::Setup();
        return;
    }

    Serial.println("[系统] 正在启动。");

    /*
     * 开机先显式关闭 WiFi。
     * 网络模块后续会通过 Network_RequestBootSync() 延迟触发自动同步，
     * 避免 setup 阶段立刻拉起 WiFi 扫描导致首屏和菜单动画变慢。
     */
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);

    SysFatUpdate::PrepareApplicationFilesystemsAtBoot();
    sysConfig.load();

    /*
     * 先把配置中的语言写入 AppManager。
     * 这样LittleFS普通指令和后续SysRes_Init都能按正确语言加载资源。
     */
    appManager.loadLanguageFromConfig();
    SysFS_Load_Prescripts();

    /*
     * SysTime_Init 设置时区并从板载 RTC 恢复时间；RTC 不可信时等待网络或手动校时。
     * 该过程只访问本地 I2C，不联网。
     * 网络对时由 Network_Init + Network_RequestBootSync 延迟完成。
     */
    SysTime_Init();
    SysMotion_Init();
    SysGesture_Init();

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
    SysUsbMode::Service();

    if (SysBootTest::Enabled())
    {
        SysBootTest::Loop();
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

    /*
     * 时间服务只在主循环消费网络结果和访问 RTC。
     * 这样 Core 0 网络任务不会直接碰 Wire1、配置文件或 UI 状态。
     */
    SysTime_Update();
    SysMotion_Update();
    SysGesture_Update();

    SysAutoPush_Update();
    appManager.run();
    delay(1);
}
