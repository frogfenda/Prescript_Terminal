/*
【模块职责】双蛇杖六种基础动作的 SYS 内部识别器接口。
【调用关系】仅由 SysGesture 在 Caduceus Profile 中喂入 SysMotion 缓存样本；识别成功后仍通过
SysGesture 的统一小队列交给 AppManager，本模块不接触 App、反馈、资源或 UI。
【重要约束】Reset/Update 只能从 Arduino 主线程调用；输入单位固定为 g、dps、us，不能直接读取 BSP。
*/
#pragma once

#include "sys/sys_gesture.h"
#include "sys/sys_motion.h"

/**
 * 清空动作窗口、静止门控和 Mahony 姿态状态。
 * 进入/离开 Caduceus Profile 以及采样超过 100ms 的真实断点时必须调用，防止半截动作跨页面或休眠。
 */
void SysCaduceusRecognizer_Reset();

/**
 * 用一帧 fresh 陀螺仪样本推进识别器。
 * 【输出】只有高置信度落入六种稳定类别时才返回 true 并写出事件；模糊、拒识、姿态预热和窗口
 * 收集中均返回 false。事件时间戳取候选触发时刻，便于后续 Furioso 按真实动作时间判断窗口。
 */
bool SysCaduceusRecognizer_Update(const SysMotionSample &sample, SysGestureEvent *out_event);
