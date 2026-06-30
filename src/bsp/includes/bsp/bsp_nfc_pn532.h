/*
【模块职责】PN532 NFC 板级驱动。负责 SPI、PN532 初始化、原始帧收发、读卡原语和复位/休眠引脚控制。
*/
#pragma once
#include <Arduino.h>

namespace BSP::NfcPn532
{
    // 【函数说明】初始化 PN532；longBootWait 用于开机等需要更长稳定时间的场景。
    bool Begin(bool longBootWait);

    // 【函数说明】按指定原因重新初始化 PN532，供故障恢复和唤醒后恢复使用。
    bool Reinitialize(const char *reason, bool longBootWait);

    // 【函数说明】返回 PN532 当前是否完成初始化并可执行读卡命令。
    bool IsReady();

    // 【函数说明】上层确认通信异常时，把 PN532 标记为离线，等待后续恢复流程。
    void MarkOffline();

    // 【函数说明】对 PN532 执行硬复位，并整理片选脚到空闲状态。
    void Reset();

    // 【函数说明】休眠前拉低 PN532 RESET 并保持电平。
    void Sleep();

    // 【函数说明】唤醒后释放 RESET hold，并把 PN532 引脚恢复到可重新初始化状态。
    void WakeupPins();

    // 【函数说明】读取 ISO14443A 被动目标 UID，是普通寻卡流程的底层封装。
    bool ReadPassiveTarget(uint8_t *uid, uint8_t *uidLen, uint16_t timeoutMs);

    // 【函数说明】执行 MIFARE Classic 指定块认证。
    bool MifareAuth(uint8_t *uid, uint8_t uidLen, uint8_t block, const uint8_t *key);

    // 【函数说明】读取 MIFARE Classic 一个 16 字节数据块。
    bool MifareReadBlock(uint8_t block, uint8_t *data);

    // 【函数说明】读取 NTAG/MIFARE Ultralight 一个 4 字节页。
    bool NtagReadPage(uint8_t page, uint8_t *data);

    // 【函数说明】设置 PN532 被动寻卡重试次数，影响 readPassiveTargetID 的等待行为。
    void SetPassiveActivationRetries(uint8_t maxRetries);

    // 【函数说明】通过 SPI 手写 PN532 原始命令帧，供上层执行库未封装的命令。
    bool RawSendCommand(const uint8_t *cmd, uint8_t cmdLen);

    // 【函数说明】读取 PN532 原始响应帧，返回实际 payload 长度，失败返回 -1。
    int RawReadResponse(uint8_t *buf, uint8_t maxLen, uint16_t timeoutMs);
}
