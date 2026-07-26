/*
【模块职责】把业力模式的只读RGB565图片渲染成可呼吸缩放、调节亮度的方形画布。
【调用关系】AppKarma每个动画帧提供当前图片、缩放和亮度；本模块只写调用方缓冲区，不访问资源路径和业务计数。
【重要约束】黑色像素保持为背景色；函数不清理主Sprite、不推屏，外部缓冲区容量必须覆盖canvasSide²像素。
*/
#pragma once

#include <Arduino.h>

#include "sys/sys_resource_io.h"

namespace UIKarma
{
    constexpr uint16_t IMAGE_CANVAS_SIDE = 96;

    /**
     * 【接口说明】将正方形源图等比缩放到canvas中心，并按brightness缩放RGB565亮度。
     * 【参数】scale建议0.85~1.0；brightness建议0.0~1.0，函数内部会限制到安全范围。
     * 【返回值】资源或缓冲区无效时返回false，缓冲区仍会被清为黑色。
     */
    bool RenderBreathingImage(uint16_t *buffer,
                              uint16_t canvasSide,
                              const SysRgb565View &image,
                              float scale,
                              float brightness);
}
