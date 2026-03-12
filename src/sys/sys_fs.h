// 文件：src/sys/sys_fs.h
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// 【独立双语内存池】
extern std::vector<String> sys_prescripts_zh;
extern std::vector<String> sys_prescripts_en;

void SysFS_Init();
void SysFS_Load_Prescripts(); // 加载双语指令库

String SysFS_Read_File(const char* filepath);
bool SysFS_Write_File(const char* filepath, const char* content);
bool SysFS_Append_File(const char* filepath, const char* content);