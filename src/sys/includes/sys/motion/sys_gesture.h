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
    // 业力应用的两个独立长边敲击语义；只会在 Karma 识别上下文中产生。
    KarmaStrikeA,
    KarmaStrikeB,

    // 双蛇杖应用的六种基础动作；只会在 Caduceus 识别上下文中产生。
    HorizontalSlash,
    VerticalSlash,
    DiagonalSlashA,
    DiagonalSlashB,
    Thrust,
    Uppercut,
};

/**
 * 离散手势识别上下文。
 * Default 保持全局滚动和换武器识别；Karma 保留滚动、启用两种长边敲击，并关闭会与敲击
 * 共用 gz 主轴的换武器判定；Caduceus 独占六种斩击/突刺识别，关闭滚动、换武器和业力。
 * 新增应用专属动作时扩展该枚举和内部策略，不新增一组零散开关。
 */
enum class SysGestureProfile : uint8_t
{
    Default = 0,
    Karma,
    Caduceus,
};

/**
 * 一次已完成识别的手势。
 * timestamp_us 使用动作触发时的 IMU 样本时间；strength_dps 是主轴峰值，便于 App 调整动画强度；
 * direction 保存物理主轴符号（+1/-1）。candidate_id、识别延迟、边界置信度和采样质量用于
 * 动作测试/离线诊断，旧 App 可继续只读取前四个稳定语义字段。confidence 是离物理分类边界
 * 的0～1分数而非统计概率；旧识别器事件固定为1，不参与双蛇杖自适应收窗判定。
 */
struct SysGestureEvent
{
    SysGestureType type = SysGestureType::None;
    uint32_t timestamp_us = 0;
    float strength_dps = 0.0f;
    int8_t direction = 0;
    uint32_t candidate_id = 0;
    uint32_t recognition_latency_us = 0;
    float confidence = 0.0f;
    float class_margin = 0.0f;
    uint16_t quality_flags = 0;
};

/** 初始化并清空识别器状态和待处理事件；必须从 Arduino 主线程调用一次。 */
void SysGesture_Init();

/**
 * 读取 SysMotion 的最新缓存样本并推进识别状态机。
 * 同一 sequence 只处理一次；函数不访问 I2C、不阻塞，也不会输出高频串口日志。
 */
void SysGesture_Update();

/**
 * 切换当前页面需要的手势识别上下文。
 * 【调用时机】只能由 Arduino 主线程中的 App 生命周期调用；进入专属页面时设置，后台或退出时
 * 恢复 Default。上下文变化会清除半截动作和待分发事件，防止专属事件跨页面泄漏。
 */
void SysGesture_SetProfile(SysGestureProfile profile);

/**
 * 【双蛇杖入口校准】在 Caduceus Profile 已切换完成后调用，开始等待用户放平设备。
 * 应用层只使用这个统一手势服务接口，不直接接触 IMU 或内部识别器。
 */
void SysGesture_BeginCaduceusEntryCalibration();

/** 返回双蛇杖入口校准是否完成；非 Caduceus Profile 始终返回 false。 */
bool SysGesture_IsCaduceusEntryCalibrationComplete();

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
