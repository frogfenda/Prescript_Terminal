/*
【模块职责】系统资源启动预热、双蛇杖分帧预热与语言资源切换的统一入口。
【设计边界】这里只协调各资源域，不暴露音频、图片、文本或身份池的裸内存。
*/
#pragma once

#include "lang/terminal_lang.h"

/**
 * 【接口说明】在FATFS挂载、语言确定且SysAudio启动后，同步预加载启动必需资源。
 * 【重复调用】初始化完成后再次调用会直接返回，不重复申请PSRAM。
 * 【失败策略】双蛇杖的大批量资源不在这里读取；首次进入对应App后才由SysRes_Update逐份加载。
 */
void SysRes_Init();

/**
 * 【接口说明】在Arduino主循环中推进最多一份双蛇杖音频或图片的预热。
 * 【调用约束】只能由主循环调用；每次最多执行一次已分块的FAT读取，不得从任务回调调用。
 * 【复位保护】若上一次复位发生在某份资源加载中，本次启动会停止预热以避免无限重启。
 */
void SysRes_Update();

/** 用户首次进入双蛇杖时启动按需预热；资源已加载或正在加载时重复调用不会重置进度。 */
void SysRes_RequestCaduceusPreload();

/**
 * 【接口说明】释放当前会话已经加载的双蛇杖音频和图片，并复位下次进入的加载进度。
 * 【调用约束】App必须先停止并确认全部自有音频实例，防止Core 0继续读取已释放PCM。
 */
void SysRes_ReleaseCaduceus();

/** 全部双蛇杖音频和图片均加载成功后返回true。 */
bool SysRes_IsCaduceusReady();

/** 素材缺失，或检测到上次在预热中复位而进入安全停用状态时返回true。 */
bool SysRes_IsCaduceusUnavailable();

/**
 * 【接口说明】运行时语言变化后立即重载所有语言相关资源；音频和图片继续复用。
 * 【调用时机】AppManager写入current_lang后、重新绘制设置页前，在Arduino主循环同步调用。
 */
void SysRes_OnLanguageChanged(SystemLang_t lang);
