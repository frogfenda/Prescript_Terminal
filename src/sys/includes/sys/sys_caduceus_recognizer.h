/*
【模块职责】双蛇杖六种基础动作的 SYS 内部识别器接口。
【调用关系】仅由 SysGesture 在 Caduceus Profile 中喂入 SysMotion 缓存样本；识别成功后仍通过
SysGesture 的统一小队列交给 AppManager，本模块不接触 App、反馈、资源或 UI。
【重要约束】Reset/Update 只能从 Arduino 主线程调用；输入单位固定为 g、dps、us，不能直接读取 BSP。
*/
#pragma once

#include "sys/sys_gesture.h"
#include "sys/sys_motion.h"
#include "sys/sys_action_frame.h"

/**
 * 最近一次已交付动作对应的人体绝对坐标影子诊断。valid=false表示该候选的人体逐帧覆盖不足，
 * 调用者只能显示“不可用”，不能据此改变动作类别；所有数值都来自共享核心已经结算的候选窗口。
 */
struct SysCaduceusShadowDiagnostics
{
    bool valid = false;
    uint32_t candidate_id = 0;
    SysActionFrame::Vector3 trajectory_direction;
    float trajectory_speed_gs = 0.0f;
    float coverage = 0.0f;
    float heading_coverage = 0.0f;
};

/**
 * 清空动作窗口、静止门控、链内预测重力和陀螺仪零偏。
 * 进入/离开 Caduceus Profile 以及采样超过 100ms 的真实断点时必须调用，防止半截动作跨页面或休眠。
 */
void SysCaduceusRecognizer_Reset();

/**
 * 【入口校准】开始一次双蛇杖页面专用的放平校准。
 * 调用后识别器会暂时屏蔽六种动作，等待屏幕正面朝上且机身静止，
 * 使用一小段连续样本平均出 Body 重力方向和陀螺仪零偏；校准完成时
 * 同时清空动作相对积分，避免进入页面前残留的姿态或角速度被当成首个动作。
 */
void SysCaduceusRecognizer_BeginEntryCalibration();

/** 返回入口放平校准是否已经通过。 */
bool SysCaduceusRecognizer_IsEntryCalibrationComplete();

/** 退出双蛇杖页面时取消入口校准状态，避免下一个页面被校准门控影响。 */
void SysCaduceusRecognizer_CancelEntryCalibration();

/**
 * 用一帧 fresh 陀螺仪样本推进识别器。
 * 【输出】只有高置信度落入六种稳定类别时才返回 true 并写出事件；模糊、拒识、姿态预热和窗口
 * 收集中均返回 false。事件时间戳取候选触发时刻，便于后续 Furioso 按真实动作时间判断窗口。
 */
bool SysCaduceusRecognizer_Update(const SysMotionSample &sample, SysGestureEvent *out_event);

/**
 * 复制最近一次SysCaduceusRecognizer_Update已交付事件对应的影子诊断。返回false表示尚无匹配
 * 事件；调用者还必须比较candidate_id，避免把上一动作的轨迹显示在新动作名下面。
 */
bool SysCaduceusRecognizer_GetLatestShadowDiagnostics(
    SysCaduceusShadowDiagnostics *out);
