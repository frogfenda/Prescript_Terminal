/*
【模块职责】持有硬币应用的正反面RGB565贴图，并向UI提供不转移所有权的只读视图。
【重要约束】六张贴图在启动时预加载并常驻到重启；UI不得释放或修改返回的像素。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_resource_io.h"

namespace SysCoinResources
{
    /** 启动预加载全部硬币贴图；缺失的彩色贴图在查询时回退到普通贴图。 */
    bool Preload();

    /**
     * 【接口说明】取得指定材质与正反面的只读图片。
     * 【参数】material为0普通、1红色、2绿色；tails为true表示反面。
     * 【返回值】目标资源缺失时回退到普通材质，普通资源也缺失时返回无效视图。
     */
    SysRgb565View GetFace(uint8_t material, bool tails);
}
