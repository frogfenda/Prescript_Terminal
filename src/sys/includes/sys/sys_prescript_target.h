/*
【模块职责】本地使用者服务接口。管理“致... / To...”替换用的本地 ID，并提供协议路由、设备菜单和指令显示共同调用的函数。
【阅读提示】这里的 ID 只用于本地显示，不关联网络账号、云端身份或远程投递。
*/
#pragma once
#include <Arduino.h>

// 【接口说明】初始化本地使用者服务，订阅蓝牙同步请求。
void SysPrescriptTarget_Init();

// 【接口说明】清洗上位机或本机输入的 ID，移除协议分隔符和换行，并限制最大长度。
String SysPrescriptTarget_Sanitize(const String &raw);

// 【接口说明】新增本地使用者 ID；已存在时视为成功并切换为当前使用者。
bool SysPrescriptTarget_Add(const String &raw_id, String *out_code = nullptr);

// 【接口说明】删除本地使用者 ID；如果删除的是当前使用者，会自动清空当前使用者。
bool SysPrescriptTarget_Delete(const String &raw_id, String *out_code = nullptr);

// 【接口说明】选择当前使用者；传入空字符串表示清空使用者。
bool SysPrescriptTarget_SetCurrent(const String &raw_id, String *out_code = nullptr);

// 【接口说明】把当前使用者应用到指令文本：中文替换“致...”，英文替换“To...”，没有模板头时按当前语言自动补前缀。
String SysPrescriptTarget_Apply(const String &raw);

// 【接口说明】把当前使用者和使用者列表通过 BLE 同步给上位机。
void SysPrescriptTarget_SyncBLE();
