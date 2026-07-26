/*
【模块职责】系统资源启动预热与语言资源切换的统一入口。
【设计边界】这里只协调各资源域，不暴露音频、图片、文本或身份池的裸内存。
*/
#pragma once

#include "lang/terminal_lang.h"

/**
 * 【接口说明】在FATFS挂载、语言确定且SysAudio启动后，预加载全部现有应用资源。
 * 【重复调用】初始化完成后再次调用会直接返回，不重复申请PSRAM。
 * 【失败策略】单个素材失败不会中止启动，由对应资源域保留空资源或业务兜底。
 */
void SysRes_Init();

/**
 * 【接口说明】运行时语言变化后立即重载所有语言相关资源；音频和图片继续复用。
 * 【调用时机】AppManager写入current_lang后、重新绘制设置页前，在Arduino主循环同步调用。
 */
void SysRes_OnLanguageChanged(SystemLang_t lang);
