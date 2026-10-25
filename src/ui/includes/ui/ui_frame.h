/*
【模块职责】UI 框架图形接口。提供战术分隔线、底部提示、危险确认弹窗和角框，供编辑页/设置页共用。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "hal/hal.h"
#include "ui/ui_theme.h"
#include "ui/ui_text.h"

namespace UIFrame {

// 【接口说明】绘制中间折角的水平分隔线，编辑页用它把标题链路和滚轮区域分开。
void DrawTacticalDivider(int y = -1, uint16_t color = UITheme::COLOR_ACCENT);
void DrawTip(const char* text, int y = -1, float fade = -1.0f);
// 【接口说明】绘制通用状态弹窗；颜色由调用方表达成功、提示或错误语义。
void DrawDialog(const char* title, const char* message, const char* tip, uint16_t color);
// 【接口说明】绘制危险确认弹窗：标题、消息、底部提示和外框，用于删除/清空等二次确认。
void DrawDangerConfirm(const char* title, const char* message, const char* tip);
void DrawCornerBox(int x1, int x2, int center_y, int half_w, int h = -1, uint16_t color = UITheme::COLOR_ACCENT);

/**
 * 绘制带薄侧面和方向反光的悬浮选中框，x/y 为左上角，w/h 为原框尺寸。
 * reflection 为平滑后的 -1～1 光照偏转；0 表示默认上亮下暗，正负值只改变边缘明暗。
 * 保留原四角高亮，不填充框内区域、不移动文字、不清屏/推屏；仅主循环调用。
 */
void DrawFloatingSelection(int x, int y, int w, int h, float reflection);

/**
 * 绘制计时运行页的细进度轨；progress 为已完成比例 0~1。
 * 轨道只画到当前 Sprite，不清屏、不推屏；用于倒计时和番茄钟共用运行态布局。
 */
void DrawTimerProgress(float progress, int y, uint16_t color = TFT_RED);

/** 绘制番茄钟轮次进度点；最多显示 12 个，避免再占用一整行文字。 */
void DrawRoundDots(uint32_t current, uint32_t total, int y);

}
