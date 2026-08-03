/*
【模块职责】绘制一个代表V4B终端的横向长条切角立方体，用不同颜色区分机身和+BodyZ屏幕面。
【几何合同】正视屏幕时BodyX是左右长边、BodyY是顶底短边，+BodyX/+BodyY处为真实右上切角；
矩形屏幕内嵌于+BodyZ正面。该合同来自屏幕几何、标签20～23、实物示意和用户确认。
【输入合同】orientation必须表示“机身坐标到观察坐标”的单位四元数；本模块只负责投影和绘制，
不读取IMU、不维护姿态、不判断校准状态。
*/
#pragma once

#include "sys/sys_pose_solver.h"

namespace UIPoseModel
{
    /** 在指定中心按正交斜视投影绘制带屏幕的长条设备；scale为世界尺寸到像素的倍率。 */
    void DrawDevice(const SysPose::Quaternion &orientation,
                    int center_x,
                    int center_y,
                    float scale);
}
