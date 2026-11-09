/*
【模块职责】LittleFS 文件工具接口。负责挂载文件系统，并提供读/写/追加文本文件；不管理FAT指令库。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/storage/sys_fs.h
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "sys/sys_psram_text.h"

// 【接口说明】挂载 LittleFS但绝不在失败时自动格式化；失败时保留原始分区并输出中文错误，后续资源和配置读取依赖挂载成功。
void SysFS_Init();

// 【接口说明】读取整个文本文件并返回 String，配置、特殊指令和网页资源调试都可使用。
String SysFS_Read_File(const char* filepath);
bool SysFS_Write_File(const char* filepath, const char* content);
// 【接口说明】在LittleFS文件末尾追加文本，不用于指令库编辑。
bool SysFS_Append_File(const char* filepath, const char* content);
