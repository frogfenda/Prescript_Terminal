/*
【模块职责】持有业力应用三个模式的RGB565图片，并提供不转移所有权的只读视图。
【调用关系】SysRes_Init在启动阶段调用Preload；AppKarma进入页面后只按模式索引读取缓存，不访问FATFS。
【重要约束】三张图片加载后常驻至重启；本模块不决定模式名称、音频、计数或绘制动画。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_resource_io.h"

namespace SysKarmaResources
{
    /**
     * 【接口说明】从/Resources/karma/images预加载image1.bin~image3.bin。
     * 【返回值】三张图片都有效时返回true；单张失败不阻止其余图片继续加载。
     * 【格式】文件必须是32~128像素范围内的无头正方形RGB565图片。
     */
    bool Preload();

    /**
     * 【接口说明】取得0~2号模式的只读图片。
     * 【返回值】索引越界或对应图片缺失时返回无效视图，不回退到其他模式。
     */
    SysRgb565View GetImage(uint8_t mode);
}
