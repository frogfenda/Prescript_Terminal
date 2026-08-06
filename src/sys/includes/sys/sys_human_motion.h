/*
【模块职责】系统级人体运动编排服务。它把SysMotion缓存中的V4B机身六轴样本、SysMag缓存中的
校准磁场、入口人体坐标追踪器和限速磁航向约束组合成一份统一只读快照。
【坐标契约】入口对齐仍要求“屏幕朝上、底边朝向使用者”：HumanX向人体右、HumanY向人体前、
HumanZ向上。absolute_linear_accel_human_g是该入口人体坐标中的去重力线性加速度，并随磁航向
修正绕Human Z稳定；它不是位置、速度，也不是把磁场当作加速度输入。
【生命周期】Init只建立未对齐服务，绝不根据任意开机姿态自动猜测人体方向。业务必须显式调用
BeginAlignment，随后由主循环在SysMotion_Update和SysMag_Update之后调用Update。
【分层边界】本服务只读取两个系统缓存，不访问BSP/I2C、文件、UI或动作识别器；SysMotion和
SysMag仍分别是IMU与地磁的唯一采样者。
*/
#pragma once

#include <stdint.h>

#include "sys/sys_human_frame_tracker.h"
#include "sys/sys_mag_aided_orientation.h"
#include "sys/sys_mag_heading_constraint.h"

namespace SysHumanMotion
{
    struct Snapshot
    {
        // false表示尚未由业务明确建立入口坐标；此时其余姿态字段只能用于显示“等待对齐”。
        bool alignment_active = false;
        uint32_t motion_sequence = 0;
        uint32_t motion_timestamp_us = 0;

        // 三层结果同时发布，便于消费者明确区分六轴基础、磁质量门与最终磁辅助输出。
        SysHumanFrame::Snapshot base;
        SysMagHeading::Snapshot magnetic_heading;
        SysMagAidedOrientation::Snapshot magnetic_orientation;

        // 与magnetic_orientation.orientation采用同一Human Z修正后的去重力加速度。
        SysHumanFrame::Vector3 absolute_linear_accel_human_g;
        bool absolute_linear_accel_valid = false;
        bool absolute_linear_accel_fresh = false;

        /*
         * 与motion_sequence/motion_timestamp_us严格对应的人体坐标角速度，单位为dps。该值先扣除
         * 本轮入口静止校准得到的陀螺零偏，再使用磁辅助Body→Human四元数旋转；地磁只通过已经
         * 质量门控、限速并可冻结的姿态间接改变坐标轴，原始磁向量不会出现在这里。
         */
        SysHumanFrame::Vector3 angular_velocity_human_dps;
        bool angular_velocity_valid = false;
    };

    /** 初始化为“尚未入口对齐”；可安全重复调用，但正常启动只调用一次。 */
    void Init();

    /**
     * 清空上一轮姿态、磁参考和统计，从后续静止样本重新建立入口人体坐标。
     * 调用者必须在Arduino主任务执行；对齐期间设备应屏幕朝上、底边朝向使用者并保持约1秒静止。
     */
    void BeginAlignment();

    /**
     * 消费SysMotion/SysMag最近缓存并推进融合。必须位于二者Update之后；重复看到同一IMU序号时
     * 不会重复积分。返回true表示本轮处理了新IMU帧或推进了对齐状态。
     */
    bool Update();

    /** 复制当前统一快照；out为空时返回false，未对齐也会返回可判状态的快照。 */
    bool GetSnapshot(Snapshot *out);
}
