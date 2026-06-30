/*
【模块职责】LittleFS 文件工具接口。负责挂载文件系统、加载普通指令池，并提供读/写/追加文本文件。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_fs.h
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// 【独立双语内存池】
extern std::vector<String> sys_prescripts_zh;
extern std::vector<String> sys_prescripts_en;

// 【接口说明】挂载 LittleFS，失败时串口输出错误，后续资源和配置读取都依赖挂载成功。
void SysFS_Init();
void SysFS_Load_Prescripts(); // 运行时版加载双语；锁定版只加载当前语言库

// 【接口说明】读取整个文本文件并返回 String，配置、特殊指令和网页资源调试都可使用。
String SysFS_Read_File(const char* filepath);
bool SysFS_Write_File(const char* filepath, const char* content);
// 【接口说明】在文件末尾追加文本，指令档案添加新条目时使用。
bool SysFS_Append_File(const char* filepath, const char* content);
