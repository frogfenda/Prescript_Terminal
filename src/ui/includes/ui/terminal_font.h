#pragma once

/*
【模块职责】终端全局字体文件入口。

本项目在当前分支采用“开发端固定单字体”策略：
- 设备用户不能切换字体；
- 开发者在这里放入或引用最终选定的 U8g2 字体数组；
- HAL 和 UI 层只通过 ui_font_config.h 取得字体，不直接引用具体字体名。

替换为自定义字体时：
1. 用 bdfconv 把 TTF/OTF 转成 U8g2 C 字体数组；
2. 把生成的 const uint8_t xxx[] 放到本文件；
3. 把 TERMINAL_FONT 指向你的字体数组名；
4. 到 ui_font_config.h 调整 baseline / lineHeight / cellWidth。

当前默认仍指向 U8g2 自带的文泉驿 GB2312 字体，确保没有自定义字体时也能完整显示中文。
*/

#include <U8g2_for_TFT_eSPI.h>

#ifndef TERMINAL_FONT
#define TERMINAL_FONT u8g2_font_wqy16_t_gb2312
#endif
