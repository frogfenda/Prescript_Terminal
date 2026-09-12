/*
【模块职责】实现 W25N01GV 隔离诊断应用。只读阶段建立器件和坏块事实；破坏性阶段只操作
固定预留的999号物理块，并在所有退出路径恢复阵列写保护。
【测试边界】本模块只判断 BSP 与单颗 NAND 的基础读写是否正常，不提供坏块映射、磨损均衡、
掉电事务或文件系统，因此测试通过不等于已经具备可投入业务的存储层。
*/
#include "sys/sys_nand_diagnostic.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

#include "bsp/bsp_flash_w25n01.h"
#include "bsp/bsp_pins.h"

namespace
{
    /*
     * W25N01GV前1000块是正常逻辑数据区，1000～1023通常留给BBM替换。实板发现低地址已有
     * FAT镜像，而块999第一页为空白，因此把正常逻辑区最后一块永久保留给诊断；测试前仍会逐页
     * 确认整块全FF，绝不根据单页抽样直接擦除。
     */
    constexpr uint16_t TEST_BLOCK = 999;
    constexpr uint16_t TEST_FIRST_PAGE = TEST_BLOCK * BSP::W25n01::PAGES_PER_BLOCK;
    constexpr size_t COMMAND_BUFFER_SIZE = 48;
    constexpr uint16_t JEDEC_REPEAT_COUNT = 256;
    constexpr uint32_t COMMAND_IDLE_SUBMIT_MS = 500;
    constexpr uint32_t MAIN_BUTTON_HOLD_MS = 3000;

    char s_command[COMMAND_BUFFER_SIZE] = {};
    size_t s_command_length = 0;
    uint32_t s_last_command_char_ms = 0;
    bool s_running = false;
    bool s_main_button_armed = false;
    bool s_main_button_triggered = false;
    uint32_t s_main_button_pressed_ms = 0;

    // 两个整页缓冲均为固定存储，不在测试过程中反复申请堆内存，便于同时排除分配失败干扰。
    uint8_t s_expected_page[BSP::W25n01::PAGE_DATA_SIZE] = {};
    uint8_t s_actual_page[BSP::W25n01::PAGE_DATA_SIZE] = {};

    struct ReadOnlySummary
    {
        bool passed = true;
        uint16_t markerCandidateCount = 0;
        uint16_t mainOnlyNonFfCount = 0;
        uint16_t spareNonFfCount = 0;
        uint16_t enabledLutCount = 0;
        uint16_t invalidLutCount = 0;
        uint16_t sampleCleanCount = 0;
        uint16_t sampleCorrectedCount = 0;
        uint16_t sampleUncorrectableCount = 0;
        uint32_t elapsedMs = 0;
    };

    void PrintSeparator(const char *title)
    {
        Serial.printf("\n[NAND诊断] ===== %s =====\n", title);
    }

    uint32_t UpdateCrc32(uint32_t crc, const uint8_t *data, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            crc ^= data[index];
            for (uint8_t bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320UL & static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U))));
        }
        return crc;
    }

    uint32_t Crc32(const uint8_t *data, size_t length)
    {
        return UpdateCrc32(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
    }

    void PrintHexPrefix(uint16_t page, const uint8_t *data, size_t length)
    {
        Serial.printf("[NAND诊断] 页%u前%u字节：", static_cast<unsigned int>(page), static_cast<unsigned int>(length));
        for (size_t index = 0; index < length; ++index)
            Serial.printf("%02X%s", data[index], index + 1 == length ? "" : " ");
        Serial.println();
    }

    void FillPattern(uint16_t page)
    {
        /*
         * 每页使用不同的确定性xorshift序列；页号和固定签名写入开头，既能发现总线位错误，
         * 也能发现地址错位或上一页数据被错误返回。测试始终整页编程，保持片内ECC的使用约束。
         */
        uint32_t state = 0x574E0107UL ^ (static_cast<uint32_t>(page) * 0x9E3779B9UL);
        for (size_t index = 0; index < sizeof(s_expected_page); ++index)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            s_expected_page[index] = static_cast<uint8_t>(state >> 24);
        }

        const char signature[] = "PRESCRIPT-NAND-TEST";
        memcpy(s_expected_page, signature, sizeof(signature) - 1);
        s_expected_page[24] = static_cast<uint8_t>(page >> 8);
        s_expected_page[25] = static_cast<uint8_t>(page & 0xFF);
        s_expected_page[26] = static_cast<uint8_t>(TEST_BLOCK);
    }

    bool IsAllErased(const uint8_t *data, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            if (data[index] != 0xFF)
                return false;
        }
        return true;
    }

    void CountEcc(BSP::W25n01::EccStatus ecc, ReadOnlySummary *summary)
    {
        if (!summary)
            return;
        switch (ecc)
        {
        case BSP::W25n01::EccStatus::Clean:
            ++summary->sampleCleanCount;
            break;
        case BSP::W25n01::EccStatus::CorrectedOneBit:
            ++summary->sampleCorrectedCount;
            break;
        case BSP::W25n01::EccStatus::Uncorrectable:
        case BSP::W25n01::EccStatus::MultipleUncorrectable:
            ++summary->sampleUncorrectableCount;
            break;
        default:
            break;
        }
    }

    bool RunJedecStabilityTest()
    {
        BSP::W25n01::JedecId id;
        const uint32_t started_ms = millis();
        for (uint16_t attempt = 0; attempt < JEDEC_REPEAT_COUNT; ++attempt)
        {
            if (!BSP::W25n01::ReadJedecId(&id) || !id.matchesW25n01gv())
            {
                Serial.printf("[NAND诊断] JEDEC稳定性失败：第%u次读取，ID=%02X %02X %02X，错误=%s。\n",
                              static_cast<unsigned int>(attempt + 1),
                              id.manufacturer,
                              id.deviceHigh,
                              id.deviceLow,
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
        }

        Serial.printf("[NAND诊断] JEDEC稳定性通过：连续%u次均为 EF AA 21，用时=%lums。\n",
                      static_cast<unsigned int>(JEDEC_REPEAT_COUNT),
                      static_cast<unsigned long>(millis() - started_ms));
        return true;
    }

    bool PrintStatus()
    {
        BSP::W25n01::Status status;
        if (!BSP::W25n01::ReadStatus(&status))
        {
            Serial.printf("[NAND诊断] 状态寄存器读取失败：错误=%s。\n",
                          BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
            return false;
        }

        Serial.printf("[NAND诊断] 状态：SR1=%02X，SR2=%02X，SR3=%02X，ECC=%s，Buffer=%s，写保护=%s，BUSY=%s。\n",
                      status.protectionRaw,
                      status.configurationRaw,
                      status.operationRaw,
                      status.eccEnabled ? "开启" : "关闭",
                      status.bufferReadMode ? "开启" : "关闭",
                      status.arrayWriteProtected ? "开启" : "关闭",
                      status.busy ? "是" : "否");
        if (status.programFailed || status.eraseFailed || status.busy ||
            !status.eccEnabled || !status.bufferReadMode || !status.arrayWriteProtected)
        {
            Serial.println("[NAND诊断] 状态检查失败：要求ECC、Buffer和写保护开启，且BUSY/P_FAIL/E_FAIL均清零。");
            return false;
        }
        return true;
    }

    bool ReadAndPrintLut(ReadOnlySummary *summary)
    {
        BSP::W25n01::BadBlockLink links[BSP::W25n01::BBM_LUT_ENTRY_COUNT];
        if (!BSP::W25n01::ReadBadBlockLut(links))
        {
            Serial.printf("[NAND诊断] BBM LUT读取失败：错误=%s。\n",
                          BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
            return false;
        }

        for (size_t index = 0; index < BSP::W25n01::BBM_LUT_ENTRY_COUNT; ++index)
        {
            if (links[index].enabled)
            {
                ++summary->enabledLutCount;
                if (links[index].invalid)
                    ++summary->invalidLutCount;
                Serial.printf("[NAND诊断] LUT[%u]：%s，LBA=%u，PBA=%u。\n",
                              static_cast<unsigned int>(index),
                              links[index].invalid ? "已失效" : "有效",
                              static_cast<unsigned int>(links[index].logicalBlock),
                              static_cast<unsigned int>(links[index].physicalBlock));
            }
        }
        Serial.printf("[NAND诊断] BBM LUT读取完成：已启用=%u，已失效=%u，空闲=%u；本测试未修改LUT。\n",
                      static_cast<unsigned int>(summary->enabledLutCount),
                      static_cast<unsigned int>(summary->invalidLutCount),
                      static_cast<unsigned int>(BSP::W25n01::BBM_LUT_ENTRY_COUNT - summary->enabledLutCount));
        return true;
    }

    bool ScanBadBlockMarkers(ReadOnlySummary *summary)
    {
        bool first_eight_spare_valid = true;
        uint16_t printed_candidates = 0;
        const uint32_t started_ms = millis();
        for (uint16_t block = 0; block < BSP::W25n01::BLOCK_COUNT; ++block)
        {
            bool bad = false;
            uint8_t markers[3] = {};
            if (!BSP::W25n01::ReadFactoryBadBlockMarker(block, &bad, markers))
            {
                Serial.printf("[NAND诊断] 坏块标记扫描失败：块=%u，错误=%s。\n",
                              static_cast<unsigned int>(block),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
            if (bad)
            {
                ++summary->markerCandidateCount;
                const bool spare_non_ff = markers[1] != 0xFF || markers[2] != 0xFF;
                if (spare_non_ff)
                {
                    ++summary->spareNonFfCount;
                    if (block < 8)
                        first_eight_spare_valid = false;
                }
                else
                {
                    ++summary->mainOnlyNonFfCount;
                }

                // 只展开前24项，避免已有大量数据时8KiB CDC启动日志被刷掉；汇总仍覆盖全部1024块。
                if (printed_candidates < 24)
                {
                    Serial.printf("[NAND诊断] 非FF候选：块=%u，主区Byte0=%02X，Spare[0..1]=%02X %02X，分类=%s。\n",
                                  static_cast<unsigned int>(block),
                                  markers[0],
                                  markers[1],
                                  markers[2],
                                  spare_non_ff ? "备用区含标记" : "仅主区非FF/可能已有数据");
                    ++printed_candidates;
                }
            }
        }

        Serial.printf("[NAND诊断] 标记扫描完成：非FF候选=%u，仅主区非FF=%u，备用区非FF=%u，用时=%lums，块0～7备用区=%s。\n",
                      static_cast<unsigned int>(summary->markerCandidateCount),
                      static_cast<unsigned int>(summary->mainOnlyNonFfCount),
                      static_cast<unsigned int>(summary->spareNonFfCount),
                      static_cast<unsigned long>(millis() - started_ms),
                      first_eight_spare_valid ? "正常" : "异常");

        /*
         * 主区Byte0只在“首次编程前”具有坏块标记含义；一旦外部工具写过数据就会产生假阳性。
         * 备用区前两字节更适合判断当前日志是否只是已有数据，但它也不能替代首次使用时保存的原始表。
         */
        if (summary->mainOnlyNonFfCount > 0)
            Serial.println("[NAND诊断] 提示：检测到仅主区Byte0非FF的块，说明芯片可能已被写入；这些块不能直接计作出厂坏块。");
        return first_eight_spare_valid && summary->spareNonFfCount <= 20;
    }

    bool RunSampleReads(ReadOnlySummary *summary)
    {
        constexpr uint16_t SAMPLE_PAGES[] = {
            0,
            TEST_FIRST_PAGE,
            static_cast<uint16_t>(999 * BSP::W25n01::PAGES_PER_BLOCK),
            static_cast<uint16_t>(BSP::W25n01::PAGE_COUNT - 1),
        };

        for (uint16_t page : SAMPLE_PAGES)
        {
            BSP::W25n01::EccStatus ecc = BSP::W25n01::EccStatus::Unknown;
            if (!BSP::W25n01::ReadPage(page, 0, s_actual_page, sizeof(s_actual_page), &ecc))
            {
                CountEcc(ecc, summary);
                Serial.printf("[NAND诊断] 抽样页读取失败：页=%u，ECC=%s，错误=%s。\n",
                              static_cast<unsigned int>(page),
                              BSP::W25n01::EccStatusName(ecc),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
            CountEcc(ecc, summary);
            const uint32_t first_crc = Crc32(s_actual_page, sizeof(s_actual_page));
            const bool erased = IsAllErased(s_actual_page, sizeof(s_actual_page));
            PrintHexPrefix(page, s_actual_page, 32);
            Serial.printf("[NAND诊断] 抽样页=%u，CRC32=%08lX，ECC=%s，全FF=%s。\n",
                          static_cast<unsigned int>(page),
                          static_cast<unsigned long>(first_crc),
                          BSP::W25n01::EccStatusName(ecc),
                          erased ? "是" : "否");

            // 同一页重复读取可区分稳定的已写数据与SPI链路偶发位错误。
            for (uint8_t repeat = 1; repeat < 8; ++repeat)
            {
                if (!BSP::W25n01::ReadPage(page, 0, s_actual_page, sizeof(s_actual_page), &ecc))
                {
                    Serial.printf("[NAND诊断] 抽样页重复读取失败：页=%u，第%u次，错误=%s。\n",
                                  static_cast<unsigned int>(page),
                                  static_cast<unsigned int>(repeat + 1),
                                  BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                    return false;
                }
                const uint32_t repeat_crc = Crc32(s_actual_page, sizeof(s_actual_page));
                if (repeat_crc != first_crc)
                {
                    Serial.printf("[NAND诊断] 抽样页重复读取不一致：页=%u，第%u次CRC32=%08lX，首次=%08lX。\n",
                                  static_cast<unsigned int>(page),
                                  static_cast<unsigned int>(repeat + 1),
                                  static_cast<unsigned long>(repeat_crc),
                                  static_cast<unsigned long>(first_crc));
                    return false;
                }
            }
            Serial.printf("[NAND诊断] 抽样页=%u连续8次读取一致。\n", static_cast<unsigned int>(page));
        }
        return true;
    }

    bool RunReadOnlyDiagnostics()
    {
        PrintSeparator("只读诊断开始");
        const uint32_t started_ms = millis();
        ReadOnlySummary summary;

        if (!BSP::W25n01::IsReady() && !BSP::W25n01::Begin())
        {
            BSP::W25n01::PrintDiagnostics();
            Serial.println("[NAND诊断] 只读诊断失败：器件未就绪，不执行任何阵列写操作。");
            return false;
        }

        BSP::W25n01::PrintDiagnostics();
        summary.passed = RunJedecStabilityTest() && summary.passed;
        summary.passed = PrintStatus() && summary.passed;
        summary.passed = ReadAndPrintLut(&summary) && summary.passed;
        summary.passed = ScanBadBlockMarkers(&summary) && summary.passed;
        summary.passed = RunSampleReads(&summary) && summary.passed;
        summary.elapsedMs = millis() - started_ms;

        Serial.printf("[NAND诊断] ECC抽样统计：无纠错=%u，已纠正1位=%u，不可纠正=%u。\n",
                      static_cast<unsigned int>(summary.sampleCleanCount),
                      static_cast<unsigned int>(summary.sampleCorrectedCount),
                      static_cast<unsigned int>(summary.sampleUncorrectableCount));
        Serial.printf("[NAND诊断] 只读诊断%s：总用时=%lums。\n",
                      summary.passed ? "通过" : "失败",
                      static_cast<unsigned long>(summary.elapsedMs));
        PrintSeparator("只读诊断结束");
        return summary.passed;
    }

    bool CheckTestBlockBlank()
    {
        for (uint16_t offset = 0; offset < BSP::W25n01::PAGES_PER_BLOCK; ++offset)
        {
            const uint16_t page = TEST_FIRST_PAGE + offset;
            BSP::W25n01::EccStatus ecc;
            if (!BSP::W25n01::ReadPage(page, 0, s_actual_page, sizeof(s_actual_page), &ecc))
            {
                Serial.printf("[NAND诊断] 测试块空白检查失败：页=%u，错误=%s。\n",
                              static_cast<unsigned int>(page),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
            if (!IsAllErased(s_actual_page, sizeof(s_actual_page)))
            {
                Serial.printf("[NAND诊断] 已拒绝破坏性测试：块%u的页%u并非全FF，可能已有数据。\n",
                              static_cast<unsigned int>(TEST_BLOCK),
                              static_cast<unsigned int>(page));
                return false;
            }
        }
        return true;
    }

    bool VerifyBlockErased()
    {
        for (uint16_t offset = 0; offset < BSP::W25n01::PAGES_PER_BLOCK; ++offset)
        {
            const uint16_t page = TEST_FIRST_PAGE + offset;
            BSP::W25n01::EccStatus ecc;
            if (!BSP::W25n01::ReadPage(page, 0, s_actual_page, sizeof(s_actual_page), &ecc) ||
                !IsAllErased(s_actual_page, sizeof(s_actual_page)))
            {
                Serial.printf("[NAND诊断] 擦除校验失败：页=%u，ECC=%s，错误=%s。\n",
                              static_cast<unsigned int>(page),
                              BSP::W25n01::EccStatusName(ecc),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
        }
        return true;
    }

    bool ProgramAndVerifyTestBlock()
    {
        for (uint16_t offset = 0; offset < BSP::W25n01::PAGES_PER_BLOCK; ++offset)
        {
            const uint16_t page = TEST_FIRST_PAGE + offset;
            FillPattern(page);
            if (!BSP::W25n01::ProgramPage(page, s_expected_page, sizeof(s_expected_page)))
            {
                Serial.printf("[NAND诊断] 顺序编程失败：页=%u，错误=%s。\n",
                              static_cast<unsigned int>(page),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
        }

        for (uint16_t offset = 0; offset < BSP::W25n01::PAGES_PER_BLOCK; ++offset)
        {
            const uint16_t page = TEST_FIRST_PAGE + offset;
            FillPattern(page);
            BSP::W25n01::EccStatus ecc;
            if (!BSP::W25n01::ReadPage(page, 0, s_actual_page, sizeof(s_actual_page), &ecc))
            {
                Serial.printf("[NAND诊断] 编程后读取失败：页=%u，ECC=%s，错误=%s。\n",
                              static_cast<unsigned int>(page),
                              BSP::W25n01::EccStatusName(ecc),
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                return false;
            }
            if (memcmp(s_expected_page, s_actual_page, sizeof(s_expected_page)) != 0)
            {
                size_t mismatch = 0;
                while (mismatch < sizeof(s_expected_page) && s_expected_page[mismatch] == s_actual_page[mismatch])
                    ++mismatch;
                Serial.printf("[NAND诊断] 数据比对失败：页=%u，列=%u，期望=%02X，实际=%02X，ECC=%s。\n",
                              static_cast<unsigned int>(page),
                              static_cast<unsigned int>(mismatch),
                              s_expected_page[mismatch],
                              s_actual_page[mismatch],
                              BSP::W25n01::EccStatusName(ecc));
                return false;
            }
        }
        return true;
    }

    void RestoreWriteProtection()
    {
        if (!BSP::W25n01::SetArrayWriteProtected(true))
        {
            Serial.printf("[NAND诊断] 严重警告：恢复阵列写保护失败，错误=%s；请立即断电并停止测试。\n",
                          BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
        }
    }

    void RunDestructiveBlockTest()
    {
        if (s_running)
            return;
        s_running = true;
        PrintSeparator("块999破坏性测试开始");
        const uint32_t started_ms = millis();
        bool passed = false;
        bool write_unlocked = false;

        do
        {
            if (!BSP::W25n01::IsReady())
            {
                Serial.println("[NAND诊断] 测试终止：NAND未就绪。");
                break;
            }

            bool marker_bad = true;
            if (!BSP::W25n01::ReadFactoryBadBlockMarker(TEST_BLOCK, &marker_bad) || marker_bad)
            {
                Serial.printf("[NAND诊断] 测试终止：块%u坏块标记异常，禁止擦除。\n",
                              static_cast<unsigned int>(TEST_BLOCK));
                break;
            }
            if (!CheckTestBlockBlank())
                break;

            if (!BSP::W25n01::SetArrayWriteProtected(false))
            {
                Serial.printf("[NAND诊断] 解除阵列写保护失败：错误=%s。\n",
                              BSP::W25n01::ErrorName(BSP::W25n01::LastError()));
                break;
            }
            write_unlocked = true;

            if (!BSP::W25n01::EraseBlock(TEST_BLOCK) || !VerifyBlockErased())
                break;
            Serial.println("[NAND诊断] 第一次块擦除及64页全FF校验通过。");

            if (!ProgramAndVerifyTestBlock())
                break;
            Serial.println("[NAND诊断] 64页顺序编程及逐字节回读校验通过。");

            // 测试结束恢复为空白块，避免测试签名被未来存储层误认为有效业务数据。
            if (!BSP::W25n01::EraseBlock(TEST_BLOCK) || !VerifyBlockErased())
                break;
            Serial.println("[NAND诊断] 收尾擦除及64页全FF校验通过。");
            passed = true;
        } while (false);

        if (write_unlocked)
            RestoreWriteProtection();
        else
            PrintStatus();

        Serial.printf("[NAND诊断] 块999破坏性测试%s：用时=%lums；BBM LUT和OTP均未修改。\n",
                      passed ? "通过" : "失败",
                      static_cast<unsigned long>(millis() - started_ms));
        PrintSeparator("块999破坏性测试结束");
        Serial.println("[NAND诊断] 可输入 SCAN 重跑只读诊断，或输入 HELP 查看命令。");
        s_running = false;
    }

    void PrintHelp()
    {
        Serial.println("[NAND诊断] 串口命令：");
        Serial.println("[NAND诊断]   SCAN          —— 重跑无损只读诊断。");
        Serial.println("[NAND诊断]   TEST BLOCK 999  —— 擦写固定测试块999；仅在该块全FF时执行。");
        Serial.println("[NAND诊断]   STATUS        —— 读取三个状态寄存器。");
        Serial.println("[NAND诊断]   HELP          —— 再次显示帮助。");
        Serial.println("[NAND诊断] 串口无法发送时，也可先松开主键，再长按主键3秒启动块999测试。");
    }

    void HandleCommand()
    {
        s_command[s_command_length] = '\0';
        for (size_t index = 0; index < s_command_length; ++index)
            s_command[index] = static_cast<char>(toupper(static_cast<unsigned char>(s_command[index])));

        if (strcmp(s_command, "SCAN") == 0)
            RunReadOnlyDiagnostics();
        else if (strcmp(s_command, "TEST BLOCK 999") == 0)
            RunDestructiveBlockTest();
        else if (strcmp(s_command, "STATUS") == 0)
            PrintStatus();
        else if (strcmp(s_command, "HELP") == 0 || s_command_length == 0)
            PrintHelp();
        else
            Serial.printf("[NAND诊断] 未识别命令：%s。输入 HELP 查看可用命令。\n", s_command);

        s_command_length = 0;
        s_command[0] = '\0';
    }

    void UpdateMainButton()
    {
        const bool pressed = digitalRead(BSP::Pins::BTN_MAIN) == LOW;
        if (!pressed)
        {
            // 必须在诊断应用启动后至少观察到一次松开，防止上电时压住按键误触发擦写。
            s_main_button_armed = true;
            s_main_button_triggered = false;
            s_main_button_pressed_ms = 0;
            return;
        }

        if (!s_main_button_armed || s_main_button_triggered || s_running)
            return;
        if (s_main_button_pressed_ms == 0)
        {
            s_main_button_pressed_ms = millis();
            Serial.println("[NAND诊断] 已检测到主键按下；持续3秒将启动块999擦写测试。");
            return;
        }
        if (millis() - s_main_button_pressed_ms < MAIN_BUTTON_HOLD_MS)
            return;

        s_main_button_triggered = true;
        Serial.println("[NAND诊断] 主键长按确认成立，开始块999擦写测试。");
        RunDestructiveBlockTest();
    }
}

namespace SysNandDiagnostic
{
    void Setup()
    {
        pinMode(BSP::Pins::BTN_MAIN, INPUT_PULLUP);
        Serial.println("\n[NAND诊断] W25N01GV隔离诊断固件已启动；正常APP、WiFi、NFC和音频均未启动。");
        RunReadOnlyDiagnostics();
        Serial.println("[NAND诊断] 请复制以上完整日志。若只读诊断通过，可输入 TEST BLOCK 999 后按回车执行单块擦写测试。");
        PrintHelp();
    }

    void Loop()
    {
        while (Serial.available() > 0)
        {
            const int value = Serial.read();
            if (value < 0)
                break;
            const char ch = static_cast<char>(value);
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                HandleCommand();
                continue;
            }
            if (s_command_length + 1 < sizeof(s_command))
            {
                s_command[s_command_length++] = ch;
                s_last_command_char_ms = millis();
            }
            else
            {
                s_command_length = 0;
                s_command[0] = '\0';
                Serial.println("[NAND诊断] 命令过长，已清空输入缓冲。");
            }
        }

        // 部分串口终端只发送字符、不附加CR/LF；静默500ms后把已收字符当作完整命令处理。
        if (s_command_length > 0 && millis() - s_last_command_char_ms >= COMMAND_IDLE_SUBMIT_MS)
            HandleCommand();

        UpdateMainButton();
        delay(2);
    }
}
