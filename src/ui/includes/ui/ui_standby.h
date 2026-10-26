/*
【模块职责】加载并绘制 LittleFS 待机图到现有 UI Sprite。
【边界】本模块只负责待机图的资源读取和占位提示；调用方负责清屏、推屏和页面生命周期。
*/
#pragma once

/** 把当前待机图载入 Sprite；资源缺失或尺寸错误时绘制诊断占位画面。 */
void UI_DrawStandbyImage();
