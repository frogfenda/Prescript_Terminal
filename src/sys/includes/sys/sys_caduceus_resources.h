/*
【模块职责】持有双蛇杖/Furioso全屏RGB565图片的PSRAM缓存，并向UI提供只读视图。
【调用关系】SysRes_Update在系统进入主循环后逐份预热；正式应用只按资源ID取图，不直接打开文件。
【重要约束】资源ID只描述真实文件，不代表九武器编号、特殊武器或动作绑定；这些业务关系必须由
正式应用的显式配置表定义，不能由SYS资源层猜测。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_resource_io.h"

/** 与/Resources/furioso/images下10个已确认文件一一对应的稳定资源ID。 */
enum class CaduceusImageId : uint8_t
{
    Axe = 0,
    Cone,
    GiantSword,
    Hammer,
    Rapier,
    Scythe,
    Spear,
    Spoon,
    Sword,
    Whip,
    Count,
};

namespace SysCaduceusResources
{
    /**
     * 每次最多把一张428x142、无文件头的RGB565图片加载到PSRAM。
     * complete在10张图片均已尝试后置true；返回值只表示本次图片是否成功。
     * 单张缺失仍会推进游标，由SysRes统一汇总最终状态。
     */
    bool PreloadStep(bool &complete);

    /**
     * 返回指定资源的只读视图。ID越界或对应文件加载失败时返回无效视图；
     * 返回指针只在当前双蛇杖应用会话内有效，调用者不得缓存、释放或修改。
     */
    SysRgb565View GetImage(CaduceusImageId id);

    /** 释放全部已加载图片并复位逐份加载游标；加载中途退出时同样可以调用。 */
    void Release();
}
