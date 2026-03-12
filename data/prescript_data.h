// 文件：src/prescript_data.h
#ifndef __PRESCRIPT_DATA_H
#define __PRESCRIPT_DATA_H

#include "app_manager.h" // 【修复】：从管家这里获取 SystemLang_t 枚举

int Get_Prescript_Count(SystemLang_t lang);
const char* Get_Prescript(SystemLang_t lang, int index);

#endif