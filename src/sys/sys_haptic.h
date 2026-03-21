// 文件：src/sys/sys_haptic.h
#pragma once
#include <Arduino.h>

class SysHaptic {
public:
    void begin(); // 初始化 I2C 总线与 LRA 线性马达闭环配置
    
    // ==========================================
    // 动作接口 (对应 DRV2605L 的内部波形库)
    // ==========================================
    void playTick();          // 极短促的滴答 (用于旋钮翻页)
    void playConfirm();       // 沉闷重击 (用于短按确认)
    void playBack();          // 快速双击 (用于长按返回/删除)
    void playCoinHeads();     // 清脆撞击 (硬币正面)
    void playCoinTails();     // 低频共振 (硬币反面)
    void playAlert();         // 刺痛警告 (接收指令闪屏)
    void playDecodeSuccess(); // 完美四连击 (解码成功)
};

extern SysHaptic sysHaptic;

// 全局宏定义 (以后在各个 App 里就直接调用这些宏！)
#define SYS_HAPTIC_NAV()         sysHaptic.playTick()
#define SYS_HAPTIC_CONFIRM()     sysHaptic.playConfirm()
#define SYS_HAPTIC_BACK()        sysHaptic.playBack()
#define SYS_HAPTIC_COIN_HEADS()  sysHaptic.playCoinHeads()
#define SYS_HAPTIC_COIN_TAILS()  sysHaptic.playCoinTails()
#define SYS_HAPTIC_ALERT()       sysHaptic.playAlert()
#define SYS_HAPTIC_DECODE()      sysHaptic.playDecodeSuccess()