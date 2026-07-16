/*
【模块职责】自动推送接口。根据配置的分钟区间随机安排下一次都市指令弹窗。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#ifndef __SYS_AUTO_PUSH_H
#define __SYS_AUTO_PUSH_H
#include <Arduino.h>

// 【手机预留接口】：未来通过蓝牙/局域网发指令，直接调用此函数即可！
// 【接口说明】写入自动推送开关和间隔范围，立即重新随机安排下一次推送时间。
void SysAutoPush_UpdateConfig(bool enable, uint32_t min_m, uint32_t max_m);

void SysAutoPush_Init();
// 【接口说明】每轮 loop 检查是否到达推送时间；成功提交统一提醒后重置并登记下一次休眠唤醒。
void SysAutoPush_Update();
void SysAutoPush_ResetTimer();

#endif
