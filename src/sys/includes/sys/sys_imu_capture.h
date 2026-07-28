/*
【模块职责】隔离固件中的 LSM6DSL 脱线采集工具。它复用 SysMotion 的缓存样本，提供屏幕选标签、
倒计时、FATFS CSV 记录和按键停止能力，使设备可以在电池供电、未连接 USB 线时完成大幅度动作采集。
【调用关系】仅在 PRESCRIPT_IMU_CAPTURE_TEST=1 时由 SysBootTest 调用 Setup/Loop；正常固件不会初始化本模块。
【重要约束】记录期间 FATFS 必须由 ESP 独占；进入 USB MSC 前必须重启，绝不允许 PC 与本模块同时访问 FAT。
*/
#pragma once

namespace SysImuCapture
{
    /** 初始化 HAL、FATFS 目录和 SysMotion；失败时留在可见错误页，不格式化 FAT 分区。 */
    void Setup();

    /** 推进输入、倒计时和104Hz定长原始样本缓存；结束采集后才统一格式化CSV并写入FAT。只能在Arduino主循环调用。 */
    void Loop();
}
