/*
【模块职责】统一物理反馈实现。把声音频率、持续时间、震动波形组合集中在一个 switch 中，让不同 App 的按键、警报、网络、NFC、抽卡反馈保持同一套触觉/听觉语言。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_feedback.cpp
#include "sys/sys_feedback.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"

void Feedback_PlayKnobTick()
{
    sysAudio.playTone(3800, 10);
    sysHaptic.playTick();
}

void Feedback_PlayConfirm()
{
    sysAudio.playTone(2800, 40);
    sysHaptic.playConfirm();
}

void Feedback_PlayBack()
{
    sysAudio.playTone(1000, 60);
    sysHaptic.playBack();
}

void Feedback_PlayError()
{
    sysAudio.playTone(300, 150);
    sysHaptic.playBack();
}

void Feedback_PlayGlitch()
{
    sysAudio.playGlitch();
    sysHaptic.playTick();
}

void Feedback_PlayAlertPulse()
{
    sysHaptic.playAlert();
    sysAudio.playTone(2500, 50);
}

void Feedback_PlayAlertSequence()
{
    sysAudio.playTone(1500, 200);
    sysAudio.playTone(1500, 200, 150);
    sysAudio.playTone(2500, 600, 300);
    sysHaptic.playAlert();
}

void Feedback_PlayDecodeComplete()
{
    sysAudio.playTone(7000, 70);
    sysAudio.playTone(7000, 70, 60);
    sysAudio.playTone(7000, 70, 120);
    sysAudio.playTone(7000, 250, 180);
    sysHaptic.playDecodeSuccess();
}

void Feedback_PlayCoinHeads()
{
    sysHaptic.playCoinHeads();
}

void Feedback_PlayCoinTails()
{
    sysHaptic.playCoinTails();
}

void Feedback_PlayGachaReveal(int star, bool isWalpurgisnacht)
{
    if (isWalpurgisnacht)
    {
        sysAudio.playTone(2500, 160);
        sysHaptic.playTick();
        return;
    }

    if (star >= 3)
    {
        sysAudio.playTone(3200, 120);
        sysHaptic.playTick();
    }
    else if (star == 2)
    {
        sysAudio.playTone(1500, 70);
    }
    else
    {
        sysAudio.playTone(800, 30);
    }
}

void Feedback_PlayNetworkOk()
{
    sysAudio.playTone(2000, 80);
    sysAudio.playTone(2500, 150, 60);
    sysHaptic.playConfirm();
}

void Feedback_PlayNetworkError()
{
    sysAudio.playTone(500, 100);
    sysHaptic.playBack();
}

void Feedback_PlayWifiDisconnected()
{
    sysAudio.playTone(800, 100);
    sysHaptic.playBack();
}

void Feedback_PlayWifiBusy()
{
    sysAudio.playTone(1500, 100);
    sysHaptic.playTick();
}

void Feedback_PlayTimerDone()
{
    sysAudio.playTone(3000, 300);
    sysHaptic.playAlert();
}

void Feedback_PlayNfcReadOk()
{
    sysAudio.playTone(4000, 50);
    sysHaptic.playConfirm();
}

void Feedback_PlayNfcReadError()
{
    sysAudio.playTone(400, 150);
    sysHaptic.playBack();
}

void Feedback_PlayWake()
{
    sysAudio.playTone(2000, 40);
    sysHaptic.playConfirm();
}

void Feedback_PlayAbort()
{
    sysAudio.playTone(800, 100);
    sysHaptic.playBack();
}

void Feedback_Play(FeedbackPattern pattern)
{
    switch (pattern)
    {
    case FeedbackPattern::KnobTick:
        Feedback_PlayKnobTick();
        break;
    case FeedbackPattern::Confirm:
        Feedback_PlayConfirm();
        break;
    case FeedbackPattern::Back:
        Feedback_PlayBack();
        break;
    case FeedbackPattern::Error:
        Feedback_PlayError();
        break;
    case FeedbackPattern::Glitch:
        Feedback_PlayGlitch();
        break;
    case FeedbackPattern::AlertPulse:
        Feedback_PlayAlertPulse();
        break;
    case FeedbackPattern::AlertSequence:
        Feedback_PlayAlertSequence();
        break;
    case FeedbackPattern::DecodeComplete:
        Feedback_PlayDecodeComplete();
        break;
    case FeedbackPattern::CoinHeads:
        Feedback_PlayCoinHeads();
        break;
    case FeedbackPattern::CoinTails:
        Feedback_PlayCoinTails();
        break;
    case FeedbackPattern::GachaReveal:
        Feedback_PlayGachaReveal(1, false);
        break;
    case FeedbackPattern::NetworkOk:
        Feedback_PlayNetworkOk();
        break;
    case FeedbackPattern::NetworkError:
        Feedback_PlayNetworkError();
        break;
    case FeedbackPattern::WifiDisconnected:
        Feedback_PlayWifiDisconnected();
        break;
    case FeedbackPattern::WifiBusy:
        Feedback_PlayWifiBusy();
        break;
    case FeedbackPattern::TimerDone:
        Feedback_PlayTimerDone();
        break;
    case FeedbackPattern::NfcReadOk:
        Feedback_PlayNfcReadOk();
        break;
    case FeedbackPattern::NfcReadError:
        Feedback_PlayNfcReadError();
        break;
    case FeedbackPattern::Wake:
        Feedback_PlayWake();
        break;
    case FeedbackPattern::Abort:
        Feedback_PlayAbort();
        break;
    }
}
