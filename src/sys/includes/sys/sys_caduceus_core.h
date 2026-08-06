/*
【模块职责】双蛇杖离散动作的可移植识别核心：统一完成静止锚定、连续双候选分段、动作局部
特征提取、六分类、边界置信度和候选诊断。固件与PC回放编译同一份实现，避免状态机双写漂移。
【分层边界】本模块不包含Arduino、PSRAM、串口、文件、App或反馈接口；调用方只按时间顺序喂入
V4B机身坐标样本。SysMotion仍是唯一采样所有者，SysGesture仍是唯一业务事件队列所有者。
【时间约束】输入单位固定为g、dps、us。超过100ms的采样断点会使动作局部积分失效，调用方
必须Reset并重新完成静止锚定；500ms仅是硬上限，高置信度且稳定收尾的动作可以提前结算。
*/
#pragma once

#include <stdint.h>

#include "sys/sys_action_frame.h"

namespace SysCaduceusCore
{
    /** 核心内部语义；包装层显式映射到SysGestureType，避免PC工具依赖Arduino业务头。 */
    enum class Gesture : uint8_t
    {
        None = 0,
        HorizontalSlash,
        VerticalSlash,
        DiagonalSlashA,
        DiagonalSlashB,
        Thrust,
        Uppercut,
    };

    /**
     * 候选累计的采样质量位。它们描述输入事实而不是分类结果；已有事件消费者可以忽略，
     * 动作测试页和离线报告则可据此区分算法边界与采集质量问题。
     */
    enum SampleQualityFlag : uint16_t
    {
        QualityNone = 0,
        QualityStaleAccel = 1u << 0,
        QualityGapOver30Ms = 1u << 1,
        QualityAccelSaturated = 1u << 2,
        QualityGyroSaturated = 1u << 3,
    };

    /** 稳定的拒识原因枚举；PC报告负责翻译文字，固件高频路径不打印候选日志。 */
    enum class RejectReason : uint8_t
    {
        Accepted = 0,
        FeatureInvalid,
        ThrustDirection,
        CutIncomplete,
        UppercutStructure,
    };

    /** 一帧已经由SysMotion转换到V4B机身坐标的输入。 */
    struct InputSample
    {
        uint32_t sequence = 0;
        uint32_t timestamp_us = 0;
        bool accel_fresh = false;
        bool gyro_fresh = false;
        SysPose::ImuSample body_imu;
        uint16_t quality_flags = QualityNone;

        /*
         * 可选的人体绝对坐标影子输入。默认false保证PC旧回放、六轴降级和未完成入口对齐时继续
         * 完整执行原BodyY翻面不变量分类；分类器禁止读取这些字段。
         */
        bool human_frame_valid = false;
        bool human_heading_stabilized = false;
        SysActionFrame::Vector3 human_linear_accel_g;
        SysActionFrame::Vector3 human_gyro_dps;
    };

    /**
     * 人体绝对坐标的候选级影子特征。它与旧Features分开，明确保证不参与当前六分类；轨迹速度
     * 单位为g*s，角面积单位为deg，coverage用于判断逐帧人体快照是否和候选完整对齐。
     */
    struct HumanShadowFeatures
    {
        bool valid = false;
        float coverage = 0.0f;
        float heading_coverage = 0.0f;
        float linear_peak_g = 0.0f;
        float linear_impulse_x_gs = 0.0f;
        float linear_impulse_y_gs = 0.0f;
        float linear_impulse_z_gs = 0.0f;
        float trajectory_peak_speed_gs = 0.0f;
        float trajectory_x = 0.0f;
        float trajectory_y = 0.0f;
        float trajectory_z = 0.0f;
        float gyro_area_x_deg = 0.0f;
        float gyro_area_y_deg = 0.0f;
        float gyro_area_z_deg = 0.0f;
    };

    /**
     * 运行时和离线报告共用的扁平特征合同。字段含义与SysActionFrame::PhaseFeatures一致，
     * 额外保存BodyY翻面不变量和原始加速度模长峰，便于报告而不让App读取原始波形。
     */
    struct Features
    {
        float gyro_peak_dps = 0.0f;
        float accel_peak_g = 0.0f;
        float integrated_x = 0.0f;
        float integrated_y = 0.0f;
        float integrated_z = 0.0f;
        float gravity_parallel = 0.0f;
        float gravity_alignment = 0.0f;
        float main_axis_longitudinal = 0.0f;
        float main_axis_xz_product = 0.0f;
        float trajectory_peak_speed_gs = 0.0f;
        float trajectory_x = 0.0f;
        float trajectory_y = 0.0f;
        float trajectory_z = 0.0f;
        float trajectory_vertical_alignment = 0.0f;
        float trajectory_longitudinal = 0.0f;
        float trajectory_xz_product = 0.0f;
        float primary_offset_ms = 0.0f;
        float primary_peak_dps = 0.0f;
        float return_peak_dps = 0.0f;
        float primary_area_deg = 0.0f;
        float return_area_deg = 0.0f;
        float return_to_primary_ratio = 0.0f;
        float main_duration_ms = 0.0f;
        float return_delay_ms = -1.0f;
        float max_main_relative_angle_deg = 0.0f;
        float end_main_relative_angle_deg = 0.0f;
        float linear_accel_peak_g = 0.0f;
        float linear_peak_x_g = 0.0f;
        float linear_peak_y_g = 0.0f;
        float linear_peak_z_g = 0.0f;
        float linear_impulse_x_gs = 0.0f;
        float linear_impulse_y_gs = 0.0f;
        float linear_impulse_z_gs = 0.0f;
    };

    /**
     * 分类结果。confidence是0～1的“离当前物理边界有多远”分数，不是统计概率；margin是
     * 当前方向扇区或专属结构相对最近竞争边界的归一化余量。二者用于诊断和保守提前结算。
     */
    struct Classification
    {
        Gesture gesture = Gesture::None;
        int8_t direction = 0;
        float confidence = 0.0f;
        float margin = 0.0f;
        RejectReason reason = RejectReason::FeatureInvalid;
    };

    /** 每个已结算候选的完整诊断；拒识候选只进入该诊断队列，不会变成业务手势事件。 */
    struct CandidateOutcome
    {
        uint32_t candidate_id = 0;
        uint32_t trigger_us = 0;
        uint32_t finished_us = 0;
        uint16_t quality_flags = QualityNone;
        bool adaptive_finished = false;
        bool features_valid = false;
        Features features;
        HumanShadowFeatures human_shadow;
        Classification classification;
    };

    /** 被接受并可交给SysGesture的轻量事件。 */
    struct GestureResult
    {
        Gesture gesture = Gesture::None;
        uint32_t timestamp_us = 0;
        uint32_t candidate_id = 0;
        uint32_t recognition_latency_us = 0;
        float strength_dps = 0.0f;
        float confidence = 0.0f;
        float class_margin = 0.0f;
        int8_t direction = 0;
        uint16_t quality_flags = QualityNone;
    };

    class Recognizer
    {
    public:
        /** 清空候选、静止证据、事件和诊断；若入口校准已启用，校准状态也重新开始。 */
        void Reset();

        /** 启用屏幕正面朝上的入口校准；完成前Update只收集静止证据，不产生候选。 */
        void BeginEntryCalibration();
        bool IsEntryCalibrationComplete() const;
        void CancelEntryCalibration();

        /**
         * 顺序推进一个fresh陀螺仪样本。返回true表示写出一条已接受语义；一次采样同时结算
         * 多个候选时，其余事件留在内部小队列，后续Update仍会先推进新样本再按顺序交付。
         */
        bool Update(const InputSample &sample, GestureResult *out_event);

        /** 取出一个已结算候选诊断。PC回放逐帧清空；固件包装层可直接丢弃以避免积压。 */
        bool PopCandidateOutcome(CandidateOutcome *outcome);

        /** 对一份已提取特征执行与运行时完全相同的六分类，供PC边界单元测试复用。 */
        static Classification ClassifyFeatures(const Features &features);

    private:
        static constexpr uint8_t RING_CAPACITY = SysActionFrame::MAX_PHASE_SAMPLES;
        static constexpr uint8_t EVENT_CAPACITY = 4;
        static constexpr uint8_t OUTCOME_CAPACITY = 8;

        struct Candidate
        {
            struct HumanFrameSample
            {
                uint32_t timestamp_us = 0;
                bool valid = false;
                bool heading_stabilized = false;
                SysActionFrame::Vector3 linear_accel_human_g;
                SysActionFrame::Vector3 gyro_human_dps;
            };

            SysActionFrame::Integrator integrator;
            SysActionFrame::FrameSample samples[RING_CAPACITY] = {};
            SysActionFrame::FrameSample linear[RING_CAPACITY] = {};
            HumanFrameSample human[RING_CAPACITY] = {};
            uint8_t head = 0;
            uint8_t count = 0;
            bool ready = false;
            bool collecting = false;
            bool waiting_valley = false;
            uint32_t candidate_id = 0;
            uint32_t trigger_us = 0;
            uint32_t settle_since_us = 0;
            uint16_t quality_flags = QualityNone;
            float accel_peak_g = 0.0f;
            SysActionFrame::Vector3 trigger_gravity = {};

            void ResetState();
        };

        Candidate candidates_[2];
        uint32_t quiet_since_us_ = 0;
        SysActionFrame::Vector3 quiet_accel_sum_ = {};
        SysActionFrame::Vector3 quiet_gyro_sum_ = {};
        uint16_t quiet_count_ = 0;
        uint32_t last_quiet_anchor_us_ = 0;
        uint32_t last_input_us_ = 0;
        float peak_energy_ = 0.0f;
        bool valley_ready_ = false;
        uint32_t valley_since_us_ = 0;
        uint32_t last_trigger_us_ = 0;
        uint32_t next_candidate_id_ = 1;
        GestureResult events_[EVENT_CAPACITY] = {};
        uint8_t event_head_ = 0;
        uint8_t event_count_ = 0;
        CandidateOutcome outcomes_[OUTCOME_CAPACITY] = {};
        uint8_t outcome_head_ = 0;
        uint8_t outcome_count_ = 0;
        bool entry_calibration_active_ = false;
        bool entry_calibration_complete_ = false;
        uint32_t entry_calibration_started_us_ = 0;

        void ResetQuietEvidence();
        void ResetPeakDetector();
        void UpdatePeakDetector(float energy, uint32_t timestamp_us);
        bool UpdateCandidate(Candidate &candidate, const InputSample &sample, uint16_t frame_quality);
        bool AnchorPrimary(const InputSample &sample);
        bool TryAnchorFromEntryQuiet(const InputSample &sample);
        bool RebasePrimary(const InputSample &sample, bool collect_now, uint16_t frame_quality);
        bool StartSecondary(const InputSample &sample, uint16_t frame_quality);
        bool Extract(Candidate &candidate, Features *features);
        bool ExtractHumanShadow(const Candidate &candidate, HumanShadowFeatures *features) const;
        void TrimPreTriggerFrames(Candidate &candidate);
        void StartCollecting(Candidate &candidate, uint32_t timestamp_us);
        void FinishCandidate(Candidate &candidate, bool is_primary,
                             const InputSample &sample, bool adaptive_finished);
        bool ShouldAdaptiveFinish(Candidate &candidate, uint32_t now_us);
        void QueueEvent(const GestureResult &event);
        bool PopEvent(GestureResult *event);
        void QueueOutcome(const CandidateOutcome &outcome);
    };
}
