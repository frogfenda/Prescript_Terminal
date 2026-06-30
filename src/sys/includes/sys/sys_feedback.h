/*
【模块职责】统一物理反馈接口。App 通过“确认、返回、错误、警报、解码完成、抽卡揭示”等语义触发反馈，内部再组合 SysAudio 与 SysHaptic。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_feedback.h
#pragma once
#include <Arduino.h>

// 统一物理反馈层：把“业务语义”映射到声音 + 震动。
// 当前阶段仍保持同步播放，以降低重构风险；后续可在此处改成异步队列。

enum class FeedbackPattern : uint8_t
{
    KnobTick,
    Confirm,
    Back,
    Error,
    Glitch,
    AlertPulse,
    AlertSequence,
    DecodeComplete,
    CoinHeads,
    CoinTails,
    GachaReveal,
    NetworkOk,
    NetworkError,
    WifiDisconnected,
    WifiBusy,
    TimerDone,
    NfcReadOk,
    NfcReadError,
    Wake,
    Abort
};

// 【接口说明】按 FeedbackPattern 分派到具体音频和震动组合，是所有语义化反馈函数的底层入口。
void Feedback_Play(FeedbackPattern pattern);

void Feedback_PlayKnobTick();
// 【接口说明】播放确认点击反馈，常用于短按进入、保存成功、NFC 手机碰触成功。
void Feedback_PlayConfirm();
void Feedback_PlayBack();
// 【接口说明】播放错误提示音和警告震动，表示协议错误、网络失败、非法设置。
void Feedback_PlayError();
void Feedback_PlayGlitch();
// 【接口说明】播放一次警报脉冲，PushNotify 闪烁期间周期触发。
void Feedback_PlayAlertPulse();
void Feedback_PlayAlertSequence();
// 【接口说明】播放解码完成的四连提示音和复合震动，对应指令成功显现。
void Feedback_PlayDecodeComplete();
void Feedback_PlayCoinHeads();
// 【接口说明】播放硬币反面的较弱确认反馈。
void Feedback_PlayCoinTails();
void Feedback_PlayGachaReveal(int star, bool isWalpurgisnacht = false);
// 【接口说明】播放网络同步成功反馈。
void Feedback_PlayNetworkOk();
void Feedback_PlayNetworkError();
// 【接口说明】播放 WiFi 断开反馈。
void Feedback_PlayWifiDisconnected();
void Feedback_PlayWifiBusy();
// 【接口说明】播放倒计时/番茄阶段完成反馈。
void Feedback_PlayTimerDone();
void Feedback_PlayNfcReadOk();
// 【接口说明】播放 NFC 读到卡但没有有效命令的错误反馈。
void Feedback_PlayNfcReadError();
void Feedback_PlayWake();
// 【接口说明】播放取消/中止反馈，副按键短按取消 NFC 伪装时使用。
void Feedback_PlayAbort();
