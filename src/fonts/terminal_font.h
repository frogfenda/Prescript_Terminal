/*
【模块职责】终端全局字体文件入口。

本项目当前采用“开发端固定单字体”策略：
- 整机只使用一套字体；
- 用户不能在设备端切换字体或字号；
- 你在开发电脑上把选好的 TTF/OTF 转换成 U8g2 字体数组后，替换本文件即可。

默认状态下，本文件把 terminal_custom_font 映射到 U8g2 自带的 wqy16 字体，
保证你还没放入自定义字体时工程仍可编译。

替换自定义字体时有两种方式：
1. 推荐：用 bdfconv 生成数组名为 terminal_custom_font 的头文件，然后直接覆盖本文件；
2. 或者保留本文件，把下面的 #define terminal_custom_font 改成你的字体数组名。

注意：
- 不要把商业字体的原始 TTF/OTF 文件提交到工程里；
- 这里只保存已经转换后的字形数组；
- 字体数组必须包含项目中会显示的中文、英文、数字和标点，否则缺字会显示为空或方块。
*/
#pragma once

#include <U8g2_for_TFT_eSPI.h>

// 默认字体：用于占位和回退。
// 如果你生成了自己的 U8g2 字体，请把数组命名为 terminal_custom_font 后覆盖本文件，
// 或者把下面这一行改成你的数组名，例如：#define terminal_custom_font my_font_16
#ifndef terminal_custom_font
#define terminal_custom_font u8g2_font_wqy15_t_gb2312
#endif
