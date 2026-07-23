/*
【模块职责】系统常驻资源缓存接口。

SysRes_Init() 在启动阶段从 FATFS 把常用音频、硬币贴图、纺织机 JSON 和身份池加载到
PSRAM。音频 PCM 不再作为全局裸指针对 App 暴露，而是在加载完成后注册给 SysAudio；
App 使用 AudioAssetId 或稳定 binding 播放，真实文件路径只由资源层持有。
*/
#pragma once

#include <Arduino.h>
#include "lang/terminal_lang.h"

extern uint16_t *g_img_heads[3];
extern uint16_t *g_img_tails[3];
// 硬币贴图边长；当前兼容 64×64 旧素材，也支持 96×96 RGB565 bin。
extern int g_img_heads_size[3];
extern int g_img_tails_size[3];

/** 提取部单条身份数据；字符串对象在 PSRAM 数组中通过 placement new 构造。 */
struct IdentityData
{
    String sinner;
    String id_name;
    int star;
    int walp;
};

extern IdentityData *g_gacha_pool;
extern int g_gacha_pool_total;
extern int *g_gacha_1star;
extern int g_count_1star;
extern int *g_gacha_2star;
extern int g_count_2star;
extern int *g_gacha_3star;
extern int g_count_3star;

// 纺织机 JSON 原始素材。sys_oracle 只解析这些已缓存内容，不直接反复访问 FATFS。
extern char *g_oracle_json_zh;
extern uint32_t g_oracle_json_zh_len;
extern char *g_oracle_json_en;
extern uint32_t g_oracle_json_en_len;

/** 挂载并注册全部常驻资源；FATFS 必须已经由启动流程完成 ESP 侧挂载。 */
void SysRes_Init();

/**
 * 【接口说明】从 FATFS 重新加载指定语言的提取部身份池，并重建星级索引。
 * 【调用时机】SysRes_Init() 在启动时调用；运行时切换语言后由 AppManager 再调用一次。
 * 【返回值】成功加载至少一条身份时返回 true；失败时清空旧语言身份池，避免继续显示错误语言。
 * 【线程约束】会释放并重建 PSRAM String 数组，只能在主循环且提取部页面未运行时调用。
 */
bool SysRes_LoadIdentityPool(SystemLang_t lang);
