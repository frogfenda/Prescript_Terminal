/*
【模块职责】硬币应用的绘制与布局实现。
*/
#include "ui/ui_coin.h"
#include "hal/hal.h"
#include "sys/sys_coin_resources.h"
#include <math.h>
#include <string.h>

namespace
{
    int clampInt(int v, int lo, int hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    int desiredCoinSizeForBucket(int maxCoinsInRow, int rows, bool skillMode)
    {
        /*
         * 硬币大小采用“分档尺寸”，不是每个硬币数量都重新算一个新大小。
         * 后续素材按 96×96 准备后，少量硬币直接使用原始显示尺寸，
         * 多枚硬币再按档位缩小；如果当前档位实在塞不下，BuildStageLayout
         * 才会做小幅回退。
         */
        maxCoinsInRow = clampInt(maxCoinsInRow, 1, UICoin::MAX_COLS_PER_ROW);
        rows = clampInt(rows, 1, 2);

        if (skillMode)
        {
            /*
             * 技能模式顶部有状态栏，但单排 1~7 枚仍应尽量占满下方舞台。
             * 6、7 枚是技能预设里最容易显得“下面空”的档位，单独加大；
             * 两行布局通常对应 10~18 枚，继续沿用紧凑尺寸，避免挤压顶部信息。
             */
            if (rows == 1)
            {
                if (maxCoinsInRow <= 2) return 90;
                if (maxCoinsInRow <= 4) return 82;
                if (maxCoinsInRow == 5) return 74;
                if (maxCoinsInRow == 6) return 62;
                if (maxCoinsInRow == 7) return 56;
            }
            if (maxCoinsInRow <= 8) return 46;
            return 40;
        }

        /*
         * 快速模式：1~4 枚直接按 96px 原始尺寸显示；5 枚单独给一个
         * 更饱满但仍能塞进 428px 宽度的档位；6 枚及之后沿用上一版
         * 相对稳定的档位。
         */
        if (rows == 1)
        {
            if (maxCoinsInRow <= 4) return 96;
            if (maxCoinsInRow == 5) return 80;
        }
        if (maxCoinsInRow <= 6) return 62;
        if (maxCoinsInRow <= 8) return 52;
        return 44;
    }

    int idealGapXForBucket(int maxCoinsInRow)
    {
        maxCoinsInRow = clampInt(maxCoinsInRow, 1, UICoin::MAX_COLS_PER_ROW);
        if (maxCoinsInRow <= 2) return 20;
        if (maxCoinsInRow <= 4) return 12;
        if (maxCoinsInRow == 5) return 7;
        if (maxCoinsInRow <= 6) return 10;
        return 6;
    }

    int idealGapYForRows(int rows)
    {
        return rows <= 1 ? 0 : 6;
    }

    bool layoutFits(int rowA, int rowB, int rows, int size, int gapX, int gapY, int areaW, int areaH)
    {
        int maxRow = max(rowA, rowB);
        int rowW = maxRow * size + max(0, maxRow - 1) * gapX;
        int blockH = rows * size + max(0, rows - 1) * gapY;
        return rowW <= areaW && blockH <= areaH;
    }

    uint16_t lighten565(uint16_t color, uint32_t intensity)
    {
        uint16_t r = (color >> 11) & 0x1F;
        uint16_t g = (color >> 5) & 0x3F;
        uint16_t b = color & 0x1F;
        r = r + (((31 - r) * intensity) >> 8);
        g = g + (((63 - g) * intensity) >> 8);
        b = b + (((31 - b) * intensity) >> 8);
        return (r << 11) | (g << 5) | b;
    }

    uint16_t dim565(uint16_t color)
    {
        uint16_t r = (color >> 11) & 0x1F;
        uint16_t g = (color >> 5) & 0x3F;
        uint16_t b = color & 0x1F;
        r = (uint16_t)(r * 2 / 3);
        g = (uint16_t)(g * 2 / 3);
        b = (uint16_t)(b * 2 / 3);
        return (r << 11) | (g << 5) | b;
    }
}

namespace UICoin
{
    StageLayout BuildStageLayout(int coinCount, int areaX, int areaY, int areaW, int areaH, bool skillMode)
    {
        StageLayout layout;
        coinCount = clampInt(coinCount, 1, MAX_COINS);
        areaW = max(areaW, COIN_RENDER_MIN + 8);
        areaH = max(areaH, COIN_RENDER_MIN + 8);

        /*
         * 一行最多 9 枚，10~18 枚自动分成两行，并尽量上下均衡。
         * 例如 10 枚为 5+5，17 枚为 9+8，18 枚为 9+9。
         */
        layout.rows = (coinCount > MAX_COLS_PER_ROW) ? 2 : 1;
        int rowCountA = (layout.rows == 1) ? coinCount : (coinCount + 1) / 2;
        int rowCountB = (layout.rows == 1) ? 0 : (coinCount - rowCountA);
        layout.cols = max(rowCountA, rowCountB);

        int desiredSize = desiredCoinSizeForBucket(layout.cols, layout.rows, skillMode);
        int desiredGapX = idealGapXForBucket(layout.cols);
        int desiredGapY = idealGapYForRows(layout.rows);
        int maxSize = skillMode ? COIN_RENDER_MAX_SKILL : COIN_RENDER_MAX_QUICK;

        /*
         * 先按分档尺寸尝试排布。只有当前档位放不下时才逐像素回退，
         * 避免 1/2、3/4、5/6 这类相邻数量反复改变硬币大小。
         */
        int size = clampInt(desiredSize, COIN_RENDER_MIN, maxSize);
        while (size > COIN_RENDER_MIN &&
               !layoutFits(rowCountA, rowCountB, layout.rows, size, desiredGapX, desiredGapY, areaW, areaH))
        {
            size--;
        }
        layout.coinSize = clampInt(size, COIN_RENDER_MIN, maxSize);

        int gapY = desiredGapY;
        if (layout.rows > 1 && layout.rows * layout.coinSize + gapY > areaH)
            gapY = max(2, areaH - layout.rows * layout.coinSize);

        int blockH = layout.rows * layout.coinSize + max(0, layout.rows - 1) * gapY;
        int originY = areaY + (areaH - blockH) / 2;
        if (originY < areaY + 1) originY = areaY + 1;

        for (int i = 0; i < MAX_COINS; ++i)
        {
            layout.x[i] = areaX;
            layout.y[i] = areaY;
        }

        for (int row = 0; row < layout.rows; ++row)
        {
            int first = (row == 0) ? 0 : rowCountA;
            int rowCount = (row == 0) ? rowCountA : rowCountB;
            if (rowCount <= 0) continue;

            int gapX = desiredGapX;
            int rowW = rowCount * layout.coinSize + max(0, rowCount - 1) * gapX;
            if (rowW > areaW && rowCount > 1)
            {
                gapX = (areaW - rowCount * layout.coinSize) / (rowCount - 1);
                if (gapX < 2) gapX = 2;
                rowW = rowCount * layout.coinSize + (rowCount - 1) * gapX;
            }

            int startX = areaX + (areaW - rowW) / 2;
            if (startX < areaX) startX = areaX;

            for (int col = 0; col < rowCount; ++col)
            {
                int idx = first + col;
                if (idx >= coinCount || idx >= MAX_COINS) break;
                layout.x[idx] = startX + col * (layout.coinSize + gapX);
                layout.y[idx] = originY + row * (layout.coinSize + gapY);
            }
        }

        return layout;
    }

    bool DrawCoinToBuffer(uint16_t *buffer, int bufferSize, const CoinFrame &frame, int targetSize)
    {
        if (!buffer || bufferSize <= 0) return false;
        targetSize = clampInt(targetSize, COIN_RENDER_MIN, bufferSize);
        memset(buffer, 0, (size_t)targetSize * (size_t)targetSize * 2);

        int material = clampInt(frame.material, 0, 2);
        const bool isBack = (frame.scaleX < 0.0f);
        const SysRgb565View image = SysCoinResources::GetFace((uint8_t)material, isBack);
        if (!image.valid() || image.width != image.height) return false;
        const uint16_t *img = image.pixels;
        const int srcSize = image.width;

        float absScale = fabsf(frame.scaleX);
        int drawW = (int)(targetSize * absScale);
        if (drawW % 2 != 0) drawW++;
        if (drawW <= 1) return true;
        if (drawW > targetSize) drawW = targetSize;

        int startX = (targetSize - drawW) / 2;
        bool isSpinning = frame.isFlipping;
        bool isFlashing = (frame.flashFrames > 0);
        bool isDimmed = (!isSpinning && frame.targetFace == 1);

        for (int dstY = 0; dstY < targetSize; ++dstY)
        {
            int srcY = dstY * srcSize / targetSize;
            if (srcY >= srcSize) srcY = srcSize - 1;
            int srcYOffset = srcY * srcSize;
            int dstYOffset = dstY * targetSize;

            for (int dstX = 0; dstX < drawW; ++dstX)
            {
                int srcX = dstX * (srcSize - 1) / max(1, drawW - 1);
                if (isBack) srcX = (srcSize - 1) - srcX;

                uint16_t color = img[srcYOffset + srcX];
                if (color == 0x0000) continue;

                if (isFlashing && !isBack && !isSpinning)
                {
                    uint32_t intensity = (uint32_t)frame.flashFrames * 256 / max(1, frame.flashDuration);
                    color = lighten565(color, intensity);
                }
                else if (isDimmed)
                {
                    color = dim565(color);
                }

                buffer[dstYOffset + startX + dstX] = color;
            }
        }

        return true;
    }
}
