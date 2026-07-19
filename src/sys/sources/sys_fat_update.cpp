/*
【模块职责】从 FAT /Update 读取离线更新包，显示安全警告与烧录进度，写入 LittleFS/OTA 分区，
并按缺失的顶级目标从 LittleFS /Backup 恢复初始文件。
【安全边界】更新只在 ESP 独占 FAT 且普通资源尚未加载时执行；固件写入下一个 OTA 槽，绝不覆盖当前槽。
*/
#include "sys/sys_fat_update.h"

#include "hal/hal.h"
#include "hal/hal_fat_storage.h"
#include "sys/sys_fs.h"
#include "sys/sys_usb_mode.h"

#include <FFat.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <algorithm>

namespace
{
    constexpr const char *LITTLEFS_PARTITION_LABEL = "spiffs";
    constexpr size_t IO_BUFFER_SIZE = 4096;
    uint8_t s_ioBuffer[IO_BUFFER_SIZE];
    bool s_updateUiReady = false;

    struct Candidate
    {
        String path;
        size_t size = 0;
        uint8_t priority = 0;
        uint8_t matches = 0;
        bool ambiguous = false;

        bool present() const { return path.length() > 0; }
    };

    struct Package
    {
        Candidate firmware;
        Candidate littlefs;

        bool present() const { return firmware.present() || littlefs.present(); }
        bool ambiguous() const { return firmware.ambiguous || littlefs.ambiguous; }
        uint8_t imageCount() const { return (firmware.present() ? 1 : 0) + (littlefs.present() ? 1 : 0); }
    };

    String leafName(const String &path)
    {
        const int slash = path.lastIndexOf('/');
        return slash >= 0 ? path.substring(slash + 1) : path;
    }

    String joinPath(const String &base, const String &leaf)
    {
        String path = base.length() ? base : String("/");
        if (!path.endsWith("/"))
            path += "/";
        path += leaf;
        return path;
    }

    bool isUpdateLeaf(const String &leaf)
    {
        return leaf.equalsIgnoreCase("Update");
    }

    bool isDirectory(const char *path)
    {
        fs::File file = FFat.open(path, FILE_READ);
        if (!file)
            return false;
        const bool directory = file.isDirectory();
        file.close();
        return directory;
    }

    bool fatPathMatchesType(const String &path, bool expectedDirectory)
    {
        fs::File file = FFat.open(path, FILE_READ);
        if (!file)
            return false;
        const bool matches = file.isDirectory() == expectedDirectory;
        file.close();
        return matches;
    }

    void considerCandidate(Candidate &candidate, const String &path, size_t size, uint8_t priority)
    {
        if (priority == 0)
            return;
        if (!candidate.present())
        {
            candidate.path = path;
            candidate.size = size;
            candidate.priority = priority;
            candidate.matches = 1;
            return;
        }
        if (candidate.path.equalsIgnoreCase(path))
            return;

        ++candidate.matches;
        candidate.ambiguous = true;
        if (priority > candidate.priority)
        {
            candidate.path = path;
            candidate.size = size;
            candidate.priority = priority;
        }
    }

    bool detectPackage(Package &package)
    {
        package = Package{};
        fs::File dir = FFat.open(SysFatUpdate::UPDATE_DIR, FILE_READ);
        if (!dir || !dir.isDirectory())
        {
            if (dir)
                dir.close();
            return false;
        }

        fs::File item = dir.openNextFile();
        while (item)
        {
            if (!item.isDirectory())
            {
                String name = leafName(String(item.name()));
                String lower = name;
                lower.toLowerCase();
                if (lower.endsWith(".bin"))
                {
                    Serial.printf("[FATFS][扫描] /Update 文件：%s，%u bytes。\n",
                                  name.c_str(), static_cast<unsigned>(item.size()));
                    uint8_t firmwarePriority = 0;
                    uint8_t littleFsPriority = 0;
                    if (lower == "firmware.bin")
                        firmwarePriority = 4;
                    else if (lower == "app.bin")
                        firmwarePriority = 3;
                    else if (lower.indexOf("firmware") >= 0)
                        firmwarePriority = 1;

                    if (lower == "littlefs.bin")
                        littleFsPriority = 4;
                    else if (lower == "spiffs.bin")
                        littleFsPriority = 3;
                    else if (lower == "fs.bin" || lower == "littelfs.bin")
                        littleFsPriority = 2;
                    else if (lower.indexOf("littlefs") >= 0 || lower.indexOf("spiffs") >= 0)
                        littleFsPriority = 1;

                    const String fullPath = joinPath(SysFatUpdate::UPDATE_DIR, name);
                    if (firmwarePriority > 0)
                        considerCandidate(package.firmware, fullPath, item.size(), firmwarePriority);
                    else if (littleFsPriority > 0)
                        considerCandidate(package.littlefs, fullPath, item.size(), littleFsPriority);
                }
            }
            item.close();
            item = dir.openNextFile();
        }
        dir.close();
        return package.present();
    }

    void ensureUpdateUi()
    {
        if (s_updateUiReady)
            return;
        HAL_Init();
        s_updateUiReady = true;
    }

    void drawCentered(const String &text, int y, HALFontRole role, uint16_t color)
    {
        const int width = HAL_Get_Text_Width_Font(text.c_str(), role);
        HAL_Screen_ShowLine_Font((HAL_Get_Screen_Width() - width) / 2, y, text.c_str(), role, color);
    }

    void drawPackageWarning(const Package &package)
    {
        ensureUpdateUi();
        HAL_Sprite_Clear();
        drawCentered("检测到离线更新包", 8, HAL_FONT_TITLE, TFT_YELLOW);

        String files = "包含：";
        if (package.littlefs.present())
            files += "LittleFS";
        if (package.littlefs.present() && package.firmware.present())
            files += " + ";
        if (package.firmware.present())
            files += "固件";
        drawCentered(files, 40, HAL_FONT_SMALL, TFT_WHITE);
        drawCentered("即将开始烧录", 70, HAL_FONT_BODY, TFT_CYAN);
        drawCentered("警告：更新过程中请勿断电", 108, HAL_FONT_BODY, TFT_RED);
        HAL_Screen_Update();
    }

    void drawProgress(const char *label, const String &path, size_t written, size_t total,
                      uint8_t stage, uint8_t stageCount, bool force)
    {
        static int lastPercent = -1;
        static uint8_t lastStage = 0;
        int percent = total == 0 ? 0 : static_cast<int>((static_cast<uint64_t>(written) * 100ULL) / total);
        percent = std::max(0, std::min(100, percent));
        if (!force && stage == lastStage && percent == lastPercent)
            return;
        lastPercent = percent;
        lastStage = stage;

        ensureUpdateUi();
        HAL_Sprite_Clear();
        String title = String("正在更新：") + (label ? label : "数据");
        drawCentered(title, 4, HAL_FONT_TITLE, TFT_CYAN);

        String stageText = String("阶段 ") + static_cast<unsigned>(stage) + "/" +
                           static_cast<unsigned>(stageCount) + "  " + leafName(path);
        drawCentered(stageText, 32, HAL_FONT_SMALL, TFT_WHITE);

        constexpr int barX = 24;
        constexpr int barY = 61;
        constexpr int barW = 380;
        constexpr int barH = 20;
        HAL_Draw_Rect(barX, barY, barW, barH, TFT_WHITE);
        HAL_Fill_Rect(barX + 2, barY + 2, barW - 4, barH - 4, TFT_DARKGREY);
        const int fill = ((barW - 4) * percent) / 100;
        if (fill > 0)
            HAL_Fill_Rect(barX + 2, barY + 2, fill, barH - 4, TFT_GREEN);

        String percentText = String(percent) + "%";
        drawCentered(percentText, 87, HAL_FONT_BODY, TFT_WHITE);
        drawCentered("请勿断电或复位设备", 116, HAL_FONT_BODY, TFT_RED);
        HAL_Screen_Update();
    }

    void drawResult(bool success, const String &detail)
    {
        ensureUpdateUi();
        HAL_Sprite_Clear();
        drawCentered(success ? "更新完成" : "更新失败", 22, HAL_FONT_TITLE, success ? TFT_GREEN : TFT_RED);
        drawCentered(detail, 61, HAL_FONT_SMALL, TFT_WHITE);
        drawCentered(success ? "设备即将重启" : "请移除错误文件后重启", 102, HAL_FONT_BODY,
                     success ? TFT_CYAN : TFT_YELLOW);
        HAL_Screen_Update();
    }

    bool readFirstByte(const String &path, uint8_t &value)
    {
        fs::File file = FFat.open(path, FILE_READ);
        if (!file || file.isDirectory())
        {
            if (file)
                file.close();
            return false;
        }
        const int byte = file.read();
        file.close();
        if (byte < 0)
            return false;
        value = static_cast<uint8_t>(byte);
        return true;
    }

    bool validatePackage(const Package &package, String &error)
    {
        if (package.ambiguous())
        {
            error = "同类镜像不止一个";
            return false;
        }

        if (package.littlefs.present())
        {
            const esp_partition_t *partition = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, LITTLEFS_PARTITION_LABEL);
            if (!partition)
            {
                error = "找不到 spiffs 分区";
                return false;
            }
            if (package.littlefs.size == 0 || package.littlefs.size > partition->size)
            {
                error = "LittleFS 镜像尺寸无效";
                return false;
            }
            Serial.printf("[FATFS][UPDATE] LittleFS target=%s image=%u/%u bytes。\n",
                          partition->label, static_cast<unsigned>(package.littlefs.size),
                          static_cast<unsigned>(partition->size));
        }

        if (package.firmware.present())
        {
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
            if (!target || target == running || target->type != ESP_PARTITION_TYPE_APP ||
                target->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
                target->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
            {
                error = "找不到可用 OTA 分区";
                return false;
            }
            if (package.firmware.size == 0 || package.firmware.size > target->size)
            {
                error = "固件镜像尺寸无效";
                return false;
            }
            uint8_t magic = 0;
            if (!readFirstByte(package.firmware.path, magic) || magic != 0xE9)
            {
                error = "固件镜像头无效";
                return false;
            }

            Serial.printf("[FATFS][UPDATE] firmware running=%s target=%s image=%u/%u bytes。\n",
                          running ? running->label : "<none>", target->label,
                          static_cast<unsigned>(package.firmware.size), static_cast<unsigned>(target->size));
        }
        return true;
    }

    bool applyImage(const Candidate &image, int command, const char *label,
                    uint8_t stage, uint8_t stageCount)
    {
        fs::File file = FFat.open(image.path, FILE_READ);
        if (!file || file.isDirectory())
        {
            if (file)
                file.close();
            Serial.printf("[FATFS][UPDATE] 打开镜像失败：%s。\n", image.path.c_str());
            return false;
        }

        Serial.printf("[FATFS][UPDATE] 开始读取并烧录：%s，%u bytes，stage=%u/%u。\n",
                      image.path.c_str(), static_cast<unsigned>(image.size),
                      static_cast<unsigned>(stage), static_cast<unsigned>(stageCount));

        if (command == U_SPIFFS)
            LittleFS.end();

        const char *partitionLabel = command == U_SPIFFS ? LITTLEFS_PARTITION_LABEL : nullptr;
        if (!Update.begin(image.size, command, -1, LOW, partitionLabel))
        {
            Serial.printf("[FATFS][UPDATE] Update.begin 失败：%s。\n", image.path.c_str());
            Update.printError(Serial);
            file.close();
            return false;
        }

        drawProgress(label, image.path, 0, image.size, stage, stageCount, true);
        Serial.printf("[FATFS][UPDATE] %s：0%% (0/%u bytes)。\n",
                      leafName(image.path).c_str(), static_cast<unsigned>(image.size));
        size_t written = 0;
        int nextLoggedPercent = 10;
        while (written < image.size)
        {
            const size_t remaining = image.size - written;
            const size_t wanted = std::min(remaining, sizeof(s_ioBuffer));
            const size_t readBytes = file.read(s_ioBuffer, wanted);
            if (readBytes == 0)
                break;
            const size_t writeBytes = Update.write(s_ioBuffer, readBytes);
            if (writeBytes != readBytes)
                break;
            written += writeBytes;
            drawProgress(label, image.path, written, image.size, stage, stageCount, false);
            const int percent = static_cast<int>((static_cast<uint64_t>(written) * 100ULL) / image.size);
            if (percent >= nextLoggedPercent)
            {
                Serial.printf("[FATFS][UPDATE] %s：%d%% (%u/%u bytes)。\n",
                              leafName(image.path).c_str(), percent,
                              static_cast<unsigned>(written), static_cast<unsigned>(image.size));
                nextLoggedPercent = ((percent / 10) + 1) * 10;
            }
            delay(1);
        }
        file.close();

        if (written != image.size)
        {
            Serial.printf("[FATFS][UPDATE] 读取或写入失败：%s (%u/%u)。\n", image.path.c_str(),
                          static_cast<unsigned>(written), static_cast<unsigned>(image.size));
            Update.printError(Serial);
            Update.abort();
            return false;
        }
        if (!Update.end() || !Update.isFinished())
        {
            Serial.printf("[FATFS][UPDATE] 镜像收尾校验失败：%s。\n", image.path.c_str());
            Update.printError(Serial);
            Update.abort();
            return false;
        }

        drawProgress(label, image.path, image.size, image.size, stage, stageCount, false);
        Serial.printf("[FATFS][UPDATE] 镜像写入完成：%s。\n", image.path.c_str());
        return true;
    }

    bool removeAppliedFile(const String &path)
    {
        for (uint8_t attempt = 0; attempt < 3; ++attempt)
        {
            (void)FFat.remove(path.c_str());
            if (!FFat.exists(path.c_str()))
            {
                Serial.printf("[FATFS][删除] 已移除完成的更新文件：%s。\n", path.c_str());
                return true;
            }
            delay(80);
        }
        Serial.printf("[FATFS][删除] 更新文件删除失败：%s。\n", path.c_str());
        return false;
    }

    bool ensureFatDirectory(const String &path)
    {
        if (!path.length() || path == "/")
            return true;
        if (FFat.exists(path.c_str()))
            return isDirectory(path.c_str());

        String normalized = path;
        if (!normalized.startsWith("/"))
            normalized = "/" + normalized;
        int cursor = 1;
        while (cursor < normalized.length())
        {
            const int slash = normalized.indexOf('/', cursor);
            const String partial = slash >= 0 ? normalized.substring(0, slash) : normalized;
            if (partial.length() && !FFat.exists(partial.c_str()))
            {
                if (!FFat.mkdir(partial.c_str()))
                {
                    Serial.printf("[FATFS][目录] 创建失败：%s。\n", partial.c_str());
                    return false;
                }
                Serial.printf("[FATFS][目录] 已创建：%s。\n", partial.c_str());
            }
            if (slash < 0)
                break;
            cursor = slash + 1;
        }
        return true;
    }

    bool treeHasFile(fs::FS &filesystem, const String &dirPath, bool ignoreRootUpdate)
    {
        fs::File dir = filesystem.open(dirPath, FILE_READ);
        if (!dir || !dir.isDirectory())
        {
            if (dir)
                dir.close();
            return false;
        }

        fs::File item = dir.openNextFile();
        while (item)
        {
            const String leaf = leafName(String(item.name()));
            const bool directory = item.isDirectory();
            item.close();
            if (ignoreRootUpdate && isUpdateLeaf(leaf))
            {
                item = dir.openNextFile();
                continue;
            }
            if (!directory)
            {
                dir.close();
                return true;
            }
            if (treeHasFile(filesystem, joinPath(dirPath, leaf), false))
            {
                dir.close();
                return true;
            }
            item = dir.openNextFile();
        }
        dir.close();
        return false;
    }

    bool copyBackupFile(const String &sourcePath, const String &targetPath)
    {
        Serial.printf("[FATFS][恢复] 复制文件：%s -> %s。\n", sourcePath.c_str(), targetPath.c_str());
        fs::File source = LittleFS.open(sourcePath, FILE_READ);
        if (!source || source.isDirectory())
        {
            if (source)
                source.close();
            return false;
        }
        const int slash = targetPath.lastIndexOf('/');
        if (slash > 0 && !ensureFatDirectory(targetPath.substring(0, slash)))
        {
            source.close();
            return false;
        }
        fs::File target = FFat.open(targetPath, FILE_WRITE);
        if (!target)
        {
            source.close();
            return false;
        }

        bool ok = true;
        while (source.available())
        {
            const size_t readBytes = source.read(s_ioBuffer, sizeof(s_ioBuffer));
            if (readBytes == 0)
                break;
            if (target.write(s_ioBuffer, readBytes) != readBytes)
            {
                ok = false;
                break;
            }
        }
        target.flush();
        target.close();
        source.close();
        if (!ok)
            (void)FFat.remove(targetPath.c_str());
        Serial.printf("[FATFS][恢复] 文件复制%s：%s。\n", ok ? "完成" : "失败", targetPath.c_str());
        return ok;
    }

    bool copyBackupTree(const String &sourceDir, const String &targetDir, bool root, size_t &copiedFiles)
    {
        fs::File dir = LittleFS.open(sourceDir, FILE_READ);
        if (!dir || !dir.isDirectory())
        {
            if (dir)
                dir.close();
            return false;
        }

        bool ok = true;
        fs::File item = dir.openNextFile();
        while (item && ok)
        {
            const String leaf = leafName(String(item.name()));
            const bool directory = item.isDirectory();
            item.close();

            if (root && isUpdateLeaf(leaf))
            {
                item = dir.openNextFile();
                continue;
            }

            const String sourceChild = joinPath(sourceDir, leaf);
            const String targetChild = joinPath(targetDir, leaf);
            if (directory)
            {
                // 空备份目录不创建；它既不是恢复载荷，也不应影响空卷判断。
                if (treeHasFile(LittleFS, sourceChild, false))
                {
                    ok = ensureFatDirectory(targetChild) &&
                         copyBackupTree(sourceChild, targetChild, false, copiedFiles);
                }
            }
            else
            {
                ok = copyBackupFile(sourceChild, targetChild);
                if (ok)
                    ++copiedFiles;
            }
            item = dir.openNextFile();
        }
        if (item)
            item.close();
        dir.close();
        return ok;
    }
}

namespace SysFatUpdate
{
    void PrepareApplicationFilesystemsAtBoot()
    {
        const bool fatMounted = HAL::FatStorage::MountForEsp();
        if (fatMounted)
        {
            Serial.println("[FATFS] 已挂载到 ESP：/fat。");

            // 必须在 LittleFS、字体、音频和普通 App 资源加载前完成更新扫描。
            const BootResult updateResult = CheckAndApplyAtBoot();
            if (updateResult != BootResult::NoPackage)
            {
                if (updateResult == BootResult::Failed)
                    Serial.println("[UPDATE] 更新失败，停止普通启动；请重启进入磁盘模式处理更新文件。");

                // 成功路径正常会在 CheckAndApplyAtBoot() 内重启；同时防止异常返回后
                // 继续使用已被刷写或状态不确定的文件系统。
                while (true)
                {
                    SysUsbMode::Service();
                    delay(10);
                }
            }
        }
        else
        {
            Serial.println("[FATFS] 挂载失败；请按住 BTN2 启动，并由 Windows 格式化磁盘。");
        }

        // 更新扫描完成后才挂载 LittleFS，确保刷写镜像时不存在旧文件句柄。
        SysFS_Init();
        if (fatMounted)
            (void)RestoreFatBackupIfNeeded();
    }

    bool EnsureUpdateDirectory()
    {
        if (!HAL::FatStorage::IsMountedForEsp())
            return false;
        if (FFat.exists(UPDATE_DIR))
        {
            if (isDirectory(UPDATE_DIR))
            {
                Serial.println("[FATFS][目录] /Update 已存在且可用。");
                return true;
            }
            Serial.println("[FATFS][目录] /Update 存在，但不是目录。");
            return false;
        }
        if (!FFat.mkdir(UPDATE_DIR))
        {
            Serial.println("[FATFS][目录] 无法创建 /Update。");
            return false;
        }
        Serial.println("[FATFS][目录] 已自动创建 /Update。");
        return true;
    }

    bool HasPendingPackage()
    {
        if (!HAL::FatStorage::IsMountedForEsp())
            return false;
        Package package;
        return detectPackage(package);
    }

    BootResult CheckAndApplyAtBoot()
    {
        Serial.println("[FATFS][扫描] 开始检查 /Update。");
        if (!EnsureUpdateDirectory())
            return BootResult::NoPackage;

        Package package;
        if (!detectPackage(package))
        {
            Serial.println("[FATFS][扫描] /Update 中没有可识别的更新包。");
            return BootResult::NoPackage;
        }

        Serial.printf("[FATFS][UPDATE] 发现更新包：firmware=%s littlefs=%s。\n",
                      package.firmware.present() ? package.firmware.path.c_str() : "<none>",
                      package.littlefs.present() ? package.littlefs.path.c_str() : "<none>");
        drawPackageWarning(package);
        delay(800);

        String validationError;
        if (!validatePackage(package, validationError))
        {
            Serial.printf("[FATFS][UPDATE] 更新包校验失败：%s。\n", validationError.c_str());
            drawResult(false, validationError);
            return BootResult::Failed;
        }

        const uint8_t stageCount = package.imageCount();
        uint8_t stage = 1;
        if (package.littlefs.present())
        {
            if (!applyImage(package.littlefs, U_SPIFFS, "文件系统", stage++, stageCount))
            {
                drawResult(false, "LittleFS 烧录失败");
                return BootResult::Failed;
            }
        }
        if (package.firmware.present())
        {
            if (!applyImage(package.firmware, U_FLASH, "固件", stage++, stageCount))
            {
                drawResult(false, "固件烧录失败");
                return BootResult::Failed;
            }
        }

        bool cleanupOk = true;
        if (package.littlefs.present() && !removeAppliedFile(package.littlefs.path))
            cleanupOk = false;
        if (package.firmware.present() && !removeAppliedFile(package.firmware.path))
            cleanupOk = false;
        if (!cleanupOk)
        {
            drawResult(false, "更新文件无法删除");
            return BootResult::Failed;
        }

        drawResult(true, "镜像校验与写入均已完成");
        Serial.println("[FATFS][UPDATE] 全部更新完成，即将重启。");
        delay(1200);
        HAL::FatStorage::UnmountFromEsp();
        delay(200);
        SysUsbMode::DisconnectBeforeRestart();
        ESP.restart();
        return BootResult::Restarting;
    }

    bool RestoreFatBackupIfNeeded()
    {
        if (!HAL::FatStorage::IsMountedForEsp())
            return false;
        if (!LittleFS.exists(BACKUP_DIR) || !treeHasFile(LittleFS, BACKUP_DIR, true))
        {
            Serial.println("[RECOVERY] no non-empty LittleFS /Backup payload");
            return false;
        }

        fs::File backupRoot = LittleFS.open(BACKUP_DIR, FILE_READ);
        if (!backupRoot || !backupRoot.isDirectory())
        {
            if (backupRoot)
                backupRoot.close();
            return false;
        }

        bool ok = true;
        size_t copiedFiles = 0;
        size_t restoredTargets = 0;
        fs::File item = backupRoot.openNextFile();
        while (item && ok)
        {
            const String leaf = leafName(String(item.name()));
            const bool directory = item.isDirectory();
            item.close();

            const String sourcePath = joinPath(BACKUP_DIR, leaf);
            const String targetPath = joinPath("/", leaf);

            // /Backup/Update 永不参与恢复；空备份目录也不算有效恢复目标。
            if (isUpdateLeaf(leaf) || (directory && !treeHasFile(LittleFS, sourcePath, false)))
            {
                item = backupRoot.openNextFile();
                continue;
            }

            const bool targetTypeMatches = fatPathMatchesType(targetPath, directory);
            const bool targetHasPayload = !directory || treeHasFile(FFat, targetPath, false);
            if (targetTypeMatches && targetHasPayload)
            {
                item = backupRoot.openNextFile();
                continue;
            }

            // 类型冲突可能是用户数据，绝不自动删除；停下并明确报告。
            if (FFat.exists(targetPath.c_str()) && !targetTypeMatches)
            {
                Serial.printf("[RECOVERY] target type mismatch, keep existing path: %s\n", targetPath.c_str());
                ok = false;
                break;
            }

            if (directory)
            {
                ok = ensureFatDirectory(targetPath) &&
                     copyBackupTree(sourcePath, targetPath, false, copiedFiles);
            }
            else
            {
                ok = copyBackupFile(sourcePath, targetPath);
                if (ok)
                    ++copiedFiles;
            }
            if (ok)
                ++restoredTargets;

            item = backupRoot.openNextFile();
        }
        if (item)
            item.close();
        backupRoot.close();

        if (restoredTargets == 0 && ok)
        {
            Serial.println("[RECOVERY] all FAT backup targets are present");
            return false;
        }

        Serial.printf("[RECOVERY] FAT backup restore %s, targets=%u files=%u\n", ok ? "complete" : "failed",
                      static_cast<unsigned>(restoredTargets), static_cast<unsigned>(copiedFiles));
        return ok;
    }
}
