/*
【模块职责】FM17550 板级驱动接口。通过当前硬件唯一引出的 UART0 访问芯片寄存器，
向 SYS 层提供 ISO14443A 寻卡、MIFARE Classic 认证/读块和 NTAG 连续四页读取能力。
【硬件边界】扩展板没有把 NPD/IRQ 引到 ESP32；硬复位由板上电路决定，运行期恢复只能使用
SoftReset 和 Soft Power-down。当前产品功能已经移除手机模拟卡，本接口只保留实体卡读取能力。
*/
#pragma once

#include <Arduino.h>

namespace BSP::NfcFm17550
{
    // 【接口说明】初始化 UART、探测 A1h 版本寄存器、软复位并开启 13.56MHz 射频场。
    bool Begin(bool longBootWait = true);

    // 【接口说明】从离线或异常状态重新建立 UART 与射频配置；扩展板未安装时安全返回 false。
    bool Reinitialize(const char *reason, bool longBootWait = false);

    bool IsReady();
    void MarkOffline();

    // 【接口说明】只做无破坏在线检查，不重新寻卡；用于后台长时间空轮询后的健康确认。
    bool HealthCheck();

    /*
     * 【接口说明】唤醒并选择一张 ISO14443A 卡，返回完整 UID 和最终 SAK。
     * uid 缓冲区至少 10 字节；uidLen 可为 4/7/10。当前实现按单卡场景工作，检测到碰撞时本轮返回 false。
     */
    bool ReadPassiveTarget(uint8_t *uid,
                           uint8_t *uidLen,
                           uint8_t *sak,
                           uint16_t timeoutMs = 80);

    // 【接口说明】使用 Key A 认证 MIFARE Classic 数据块；认证使用 UID 的末四字节。
    bool MifareAuth(const uint8_t *uid,
                    uint8_t uidLen,
                    uint8_t block,
                    const uint8_t key[6]);

    // 【接口说明】读取一个 MIFARE Classic 16 字节数据块，并校验卡片返回的 CRC_A。
    bool MifareReadBlock(uint8_t block, uint8_t data[16]);

    // 【接口说明】从 startPage 开始读取 NTAG/Ultralight 连续四页，共 16 字节。
    bool NtagReadFourPages(uint8_t startPage, uint8_t data[16]);

    // 【接口说明】关闭射频并进入 Soft Power-down；没有硬件复位线，因此不能进入 Deep Power-down。
    void Sleep();

    // 【接口说明】按手册 UART 唤醒序列退出 Soft Power-down；完整恢复仍由 Reinitialize 完成。
    void Wakeup();
}
