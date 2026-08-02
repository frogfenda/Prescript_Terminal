/*
【模块职责】把SysMotion机身坐标样本和共享SysCaduceusCore连接到统一SysGestureEvent。
【分层边界】本包装层只负责PSRAM生命周期、原始量程质量标记和枚举映射；不重复分段/分类，
不读取I2C，不接触App、UI、音频或反馈。PC回放与固件实际运行同一份核心实现。
*/
#include "sys/sys_caduceus_recognizer.h"

#include <new>

#include <esp_heap_caps.h>

#include "sys/sys_caduceus_core.h"

namespace
{
    SysCaduceusCore::Recognizer *s_recognizer = nullptr;
    bool s_allocation_failed = false;

    bool EnsureRecognizer()
    {
        if (s_recognizer)
            return true;
        if (s_allocation_failed)
            return false;

        /*
         * 核心内含两个96帧候选环、提取暂存区和诊断队列，只在首次使用时一次性放入PSRAM。
         * 这些数据不参与DMA/ISR；placement new必须执行默认成员初始化，不能只清零裸内存。
         */
        void *storage = heap_caps_malloc(sizeof(SysCaduceusCore::Recognizer),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage)
        {
            s_allocation_failed = true;
            Serial.printf("[动作识别] 双蛇杖共享核心申请PSRAM失败，需要=%u字节；本次启动停用专属动作识别。\n",
                          static_cast<unsigned>(sizeof(SysCaduceusCore::Recognizer)));
            return false;
        }
        s_recognizer = new (storage) SysCaduceusCore::Recognizer();
        s_recognizer->Reset();
        return true;
    }

    bool NearRawLimit(int16_t value)
    {
        const int32_t wide = static_cast<int32_t>(value);
        return wide >= 32760 || wide <= -32760;
    }

    uint16_t BuildQualityFlags(const SysMotionSample &sample)
    {
        uint16_t flags = SysCaduceusCore::QualityNone;
        if (NearRawLimit(sample.ax_raw) || NearRawLimit(sample.ay_raw) ||
            NearRawLimit(sample.az_raw))
        {
            flags = static_cast<uint16_t>(flags | SysCaduceusCore::QualityAccelSaturated);
        }
        if (NearRawLimit(sample.gx_raw) || NearRawLimit(sample.gy_raw) ||
            NearRawLimit(sample.gz_raw))
        {
            flags = static_cast<uint16_t>(flags | SysCaduceusCore::QualityGyroSaturated);
        }
        return flags;
    }

    SysGestureType MapGesture(SysCaduceusCore::Gesture gesture)
    {
        switch (gesture)
        {
        case SysCaduceusCore::Gesture::HorizontalSlash:
            return SysGestureType::HorizontalSlash;
        case SysCaduceusCore::Gesture::VerticalSlash:
            return SysGestureType::VerticalSlash;
        case SysCaduceusCore::Gesture::DiagonalSlashA:
            return SysGestureType::DiagonalSlashA;
        case SysCaduceusCore::Gesture::DiagonalSlashB:
            return SysGestureType::DiagonalSlashB;
        case SysCaduceusCore::Gesture::Thrust:
            return SysGestureType::Thrust;
        case SysCaduceusCore::Gesture::Uppercut:
            return SysGestureType::Uppercut;
        case SysCaduceusCore::Gesture::None:
        default:
            return SysGestureType::None;
        }
    }
}

void SysCaduceusRecognizer_Reset()
{
    if (EnsureRecognizer())
        s_recognizer->Reset();
}

void SysCaduceusRecognizer_BeginEntryCalibration()
{
    if (EnsureRecognizer())
        s_recognizer->BeginEntryCalibration();
}

bool SysCaduceusRecognizer_IsEntryCalibrationComplete()
{
    return EnsureRecognizer() && s_recognizer->IsEntryCalibrationComplete();
}

void SysCaduceusRecognizer_CancelEntryCalibration()
{
    if (EnsureRecognizer())
        s_recognizer->CancelEntryCalibration();
}

bool SysCaduceusRecognizer_Update(const SysMotionSample &sample, SysGestureEvent *out_event)
{
    if (!out_event || !sample.gyro_fresh || !EnsureRecognizer())
        return false;

    SysCaduceusCore::InputSample input = {};
    input.sequence = sample.sequence;
    input.timestamp_us = sample.timestamp_us;
    input.accel_fresh = sample.accel_fresh;
    input.gyro_fresh = sample.gyro_fresh;
    input.body_imu = sample.body_imu;
    input.quality_flags = BuildQualityFlags(sample);

    SysCaduceusCore::GestureResult result = {};
    const bool recognized = s_recognizer->Update(input, &result);

    /*
     * 固件不输出每个拒识候选的高频日志；仍要及时弹空共享核心诊断队列，避免旧记录在固定
     * 容量中积压。PC宿主会消费这些记录生成召回、误触、延迟和采样质量报告。
     */
    SysCaduceusCore::CandidateOutcome discarded = {};
    while (s_recognizer->PopCandidateOutcome(&discarded))
    {
    }

    if (!recognized)
        return false;
    out_event->type = MapGesture(result.gesture);
    out_event->timestamp_us = result.timestamp_us;
    out_event->strength_dps = result.strength_dps;
    out_event->direction = result.direction;
    out_event->candidate_id = result.candidate_id;
    out_event->recognition_latency_us = result.recognition_latency_us;
    out_event->confidence = result.confidence;
    out_event->class_margin = result.class_margin;
    out_event->quality_flags = result.quality_flags;
    return out_event->type != SysGestureType::None;
}
