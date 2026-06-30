/*
【模块职责】DRV2605L 震动接口。上层按语义调用 tick/confirm/back/alert/decodeSuccess，具体 ROM 波形号和强度映射在 sys_haptic.cpp。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_haptic.h
#pragma once
#include <Arduino.h>

class SysHaptic
{
public:
    // 【函数说明】初始化 DRV2605L 的 I2C 总线和内部 LRA 波形库。
    void begin(); // 初始化 I2C 总线与 LRA 线性马达闭环配置

    // ==========================================
    // 动作接口 (对应 DRV2605L 的内部波形库)
    // ==========================================
    // 【函数说明】旋钮移动的极短微震。
    void playTick();          // 极短促的滴答 (用于旋钮翻页)
    void playConfirm();       // 沉闷重击 (用于短按确认)
    void playBack();          // 快速双击 (用于长按返回/删除)
    void playCoinHeads();     // 清脆撞击 (硬币正面)
    // 【函数说明】硬币反面结果反馈。
    void playCoinTails();     // 低频共振 (硬币反面)
    void playAlert();         // 刺痛警告 (接收指令闪屏)
    void playDecodeSuccess(); // 完美四连击 (解码成功)
    void SysHaptic_Sleep();
    // 【接口说明】让 DRV2605L 回到 internal trigger。
    void SysHaptic_Wakeup();
};

extern SysHaptic sysHaptic;

// 【接口说明】让 DRV2605L 进入 standby。
void SysHaptic_Sleep();
void SysHaptic_Wakeup();

#include "sys/sys_feedback.h"

// 全局宏定义：统一转到反馈层，后续只需调整 sys_feedback.cpp。
#define SYS_HAPTIC_NAV() Feedback_PlayKnobTick()
#define SYS_HAPTIC_CONFIRM() Feedback_PlayConfirm()
#define SYS_HAPTIC_BACK() Feedback_PlayBack()
#define SYS_HAPTIC_COIN_HEADS() Feedback_PlayCoinHeads()
#define SYS_HAPTIC_COIN_TAILS() Feedback_PlayCoinTails()
#define SYS_HAPTIC_ALERT() Feedback_PlayAlertPulse()
#define SYS_HAPTIC_DECODE() sysHaptic.playDecodeSuccess()
