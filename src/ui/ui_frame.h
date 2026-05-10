/*
【模块职责】UI 框架图形接口。提供战术分隔线、底部提示、危险确认弹窗和角框，供编辑页/设置页共用。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#pragma once
#include <Arduino.h>
#include "../hal/hal.h"
#include "ui_theme.h"
#include "ui_text.h"

namespace UIFrame {

// 【接口说明】绘制中间折角的水平分隔线，编辑页用它把标题链路和滚轮区域分开。
void DrawTacticalDivider(int y = UITheme::EditFlow::DividerY, uint16_t color = UITheme::COLOR_ACCENT);
void DrawTip(const char* text, int y = UITheme::EditFlow::TipY, float fade = UITheme::EditFlow::TipFade);
// 【接口说明】绘制危险确认弹窗：标题、消息、底部提示和外框，用于删除/清空等二次确认。
void DrawDangerConfirm(const char* title, const char* message, const char* tip);
void DrawCornerBox(int x1, int x2, int center_y, int half_w, int h = 16, uint16_t color = UITheme::COLOR_ACCENT);

}
