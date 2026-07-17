/*
【模块职责】把 SysMotion 缓存的六轴样本转换为少量、稳定的离散手势事件。
【分层边界】本模块只负责识别“上滚、下滚、换武器”等语义，不读取 BSP、不直接操作页面，
也不播放反馈；AppManager 和具体 App 决定事件最终产生什么交互。
【调用关系】setup() 在 SysMotion_Init() 后调用 SysGesture_Init()；主循环每次完成
SysMotion_Update() 后调用 SysGesture_Update()；主线程消费者用 SysGesture_PopEvent() 取走事件。
*/
#pragma once

#include <Arduino.h>

/** 统一的离散运动手势类型；连续姿态/流体输入不经过这个枚举。 */
enum class SysGestureType : uint8_t
{
    None = 0,
    ScrollUp,
    ScrollDown,
    WeaponChange,
};

/**
 * 一次已完成识别的手势。
 * timestamp_us 使用产生事件的 IMU 样本时间；strength_dps 是主轴峰值，便于 App 调整动画强度；
 * direction 保存物理主轴符号（+1/-1），换武器应用以后可以按旋转方向选择前后武器。
 */
struct SysGestureEvent
{
    SysGestureType type = SysGestureType::None;
    uint32_t timestamp_us = 0;
    float strength_dps = 0.0f;
    int8_t direction = 0;
};

/** 初始化并清空识别器状态和待处理事件；必须从 Arduino 主线程调用一次。 */
void SysGesture_Init();

/**
 * 读取 SysMotion 的最新缓存样本并推进识别状态机。
 * 同一 sequence 只处理一次；函数不访问 I2C、不阻塞，也不会输出高频串口日志。
 */
void SysGesture_Update();

/**
 * 从内部小队列取出最早的一条手势事件。
 * 返回 false 表示暂无事件或 out 为空；事件被成功返回后即从队列移除，只应在主线程消费。
 */
bool SysGesture_PopEvent(SysGestureEvent *out);

/**
 * 清空正在跟踪的半截动作和待处理事件。
 * 适合测试模式或未来切换识别策略时使用；普通 Light Sleep 的采样间隔会被 Update 自动识别。
 */
void SysGesture_Reset();
