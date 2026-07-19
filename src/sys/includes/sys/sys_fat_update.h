#pragma once

#include <Arduino.h>

/*
 * FAT 拖拽更新与初始数据恢复。
 *
 * - 更新包目录固定为 FAT 根目录下的 /Update；目录不存在时自动创建。
 * - firmware.bin/app.bin 写入下一个 OTA App 分区。
 * - littlefs.bin/spiffs.bin/fs.bin 写入标签为 spiffs 的 LittleFS 分区。
 * - LittleFS /Backup 可作为 FAT 初始内容备份；恢复时按备份中的顶级目标检查缺失项，
 *   忽略根级 /Update 和所有空目录，避免自动创建 Update 后阻止恢复。
 *
 * 下列接口仅可在 HAL::FatStorage 已由 ESP 挂载时调用。
 */
namespace SysFatUpdate
{
    constexpr const char *UPDATE_DIR = "/Update";
    constexpr const char *BACKUP_DIR = "/Backup";

    enum class BootResult : uint8_t
    {
        NoPackage,
        Failed,
        Restarting,
    };

    bool EnsureUpdateDirectory();
    bool HasPendingPackage();

    // 普通启动的文件系统入口：挂载 FAT、在 LittleFS 初始化前处理更新，随后初始化
    // LittleFS 并按需恢复 /Backup。更新失败时停在错误页，不会继续加载应用资源。
    void PrepareApplicationFilesystemsAtBoot();

    // 成功时清理更新文件并立即重启；失败时保留文件和错误画面，交由调用方停止普通启动。
    BootResult CheckAndApplyAtBoot();

    // 从 LittleFS /Backup 恢复 FAT 中缺失的顶级备份目标；不覆盖类型正确的现有目标。
    bool RestoreFatBackupIfNeeded();
}
