/*
【模块职责】待机图读取和占位画面。
【线程约束】仅在 Arduino 主循环调用；不创建任务、不进入休眠、不直接控制背光。
*/
#include "ui/ui_standby.h"

#include <LittleFS.h>

#include "hal/hal.h"
#include "sys/sys_constants.h"

void UI_DrawStandbyImage()
{
    HAL_Sprite_Clear();

    const size_t expected_bytes = (size_t)HAL_Get_Screen_Width() * HAL_Get_Screen_Height() * 2;
    File file = LittleFS.open(PrescriptConst::STANDBY_IMAGE_BIN, "r");
    if (!file)
        file = LittleFS.open(PrescriptConst::STANDBY_IMAGE_LEGACY_BIN, "r");

    if (!file)
    {
        Serial.println("[显示] 待机图不存在：/common/standby.bin。");
        HAL_Draw_Rect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_RED);
        HAL_Screen_ShowTextLine(12, 12, "NO standby.bin");
        return;
    }

    if ((size_t)file.size() < expected_bytes)
    {
        Serial.printf("[显示] 待机图尺寸过小：当前 %lu 字节，需要 %lu 字节。请重新导出 428x142 RGB565。\n",
                      (unsigned long)file.size(),
                      (unsigned long)expected_bytes);
        file.close();
        HAL_Draw_Rect(0, 0, HAL_Get_Screen_Width(), HAL_Get_Screen_Height(), TFT_ORANGE);
        HAL_Screen_ShowTextLine(12, 12, "standby.bin size mismatch");
        return;
    }

    // 逐行复制文件原始字节，保持 standby.bin 的既有 RGB565 字节序，
    // 同时避免 UI 直接接触 TFT Sprite 对象或申请第二块全屏缓冲区。
    static uint8_t line[428 * sizeof(uint16_t)];
    for (uint16_t y = 0; y < HAL_Get_Screen_Height(); ++y)
    {
        const size_t row_bytes = (size_t)HAL_Get_Screen_Width() * sizeof(uint16_t);
        if (file.read(line, row_bytes) != row_bytes)
            break;
        HAL_Sprite_CopyRawRow(y, line, row_bytes);
    }
    file.close();
}
