/*
【模块职责】TM6605 震动接口。上层按语义调用 tick/confirm/back/alert/decodeSuccess，具体 TM6605 效果号和强度映射在 BSP 中维护。
【阅读提示】业务层不要直接写触觉芯片寄存器；统一通过 SysHaptic 或 sys_feedback 触发。
*/
#pragma once
#include <Arduino.h>

class SysHaptic
{
public:
    // 【函数说明】初始化 TM6605 的 I2C 总线和触觉效果播放能力。
    void begin();

    void playTick();          // 旋钮翻页/轻操作反馈
    void playConfirm();       // 短按确认
    void playBack();          // 返回/删除/错误类反馈
    void playCoinHeads();     // 硬币正面
    void playCoinTails();     // 硬币反面
    void playAlert();         // 警报/提醒
    void playDecodeSuccess(); // 解码完成
    void SysHaptic_Sleep();
    void SysHaptic_Wakeup();
};

extern SysHaptic sysHaptic;

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
