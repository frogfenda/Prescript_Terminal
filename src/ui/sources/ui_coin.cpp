/*
【模块职责】硬币应用的绘制与布局实现。
*/
#include "ui/ui_coin.h"
#include "hal/hal.h"
#include "sys/resources/sys_coin_resources.h"
#include <math.h>
#include <string.h>

namespace
{
    constexpr float COIN_PI = 3.14159265358979323846f;
    constexpr float COIN_TURN = 2.0f * COIN_PI;
    // 保留原有 16ms 转 0.6rad 的快速翻面观感，改成由实际经过时间求姿态。
    constexpr float SPIN_RADIANS_PER_MS = 0.6f / 16.0f;
    constexpr uint32_t PHASE_BLEND_MS = 120;

    float wrapAngle(float angle)
    {
        return fmodf(angle, COIN_TURN);
    }

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

    uint16_t tint565(uint16_t color, uint16_t tint, uint32_t intensity)
    {
        // 两端加权均为非负数，红闪降低绿/蓝通道时也不会发生无符号下溢。
        const uint32_t original = 256 - intensity;
        const uint16_t r = ((((color >> 11) & 31) * original) + (((tint >> 11) & 31) * intensity)) >> 8;
        const uint16_t g = ((((color >> 5) & 63) * original) + (((tint >> 5) & 63) * intensity)) >> 8;
        const uint16_t b = (((color & 31) * original) + ((tint & 31) * intensity)) >> 8;
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
    void FlipAnimator::reset()
    {
        state_ = State::Idle;
        started_ms_ = 0;
        angle_ = start_angle_ = target_angle_ = 0.0f;
        phase_offset_ = settle_tangent_ = 0.0f;
    }

    void FlipAnimator::start(uint32_t now, float phaseOffset)
    {
        // 重投从上一轮的实际正/反面接续，不先跳回正面；新时间原点避免跨轮累计误差。
        state_ = State::Spinning;
        started_ms_ = now;
        start_angle_ = angle_;
        phase_offset_ = constrain(phaseOffset, 0.0f, 1.4f);
    }

    void FlipAnimator::settle(uint32_t now, bool tails)
    {
        if (!isSpinning()) return;
        update(now);
        // 提前手动停币时，相位展开可能尚未结束；收尾必须接续这一刻的真实角速度。
        const float phaseT = min(1.0f, (float)(now - started_ms_) / PHASE_BLEND_MS);
        const float speed = SPIN_RADIANS_PER_MS
            + phase_offset_ * 6.0f * phaseT * (1.0f - phaseT) / PHASE_BLEND_MS;
        settle_tangent_ = speed * SETTLE_MS;
        started_ms_ = now;
        start_angle_ = angle_;
        const float tangent = settle_tangent_;
        const float face = tails ? COIN_PI : 0.0f;
        /*
         * 三次 Hermite 曲线以原转速进入、以零转速退出。目标角只向前选择同一面的
         * 整周等价角，且距离至少为 tangent/3，使曲线导数始终非负，避免倒转或越过
         * 目标再拉回。收尾限制为60ms，以快速锁定目标面，不模拟长时间自由落体。
         */
        const float minimum = start_angle_ + tangent / 3.0f;
        target_angle_ = face + ceilf((minimum - face) / COIN_TURN) * COIN_TURN;
        state_ = State::Settling;
    }

    bool FlipAnimator::update(uint32_t now)
    {
        const uint32_t elapsed = now - started_ms_;
        if (state_ == State::Spinning)
        {
            // 把整周时间先消去，长时间手动等待也不会把一个很大的角度交给 sin/cos。
            const float turnMs = COIN_TURN / SPIN_RADIANS_PER_MS;
            // smoothstep 在起止两端导数为零：保留重投首帧朝向，展开后回到统一转速。
            const float t = min(1.0f, (float)elapsed / PHASE_BLEND_MS);
            const float phase = phase_offset_ * t * t * (3.0f - 2.0f * t);
            angle_ = wrapAngle(start_angle_ + fmodf((float)elapsed, turnMs) * SPIN_RADIANS_PER_MS + phase);
        }
        else if (state_ == State::Settling)
        {
            if (elapsed >= SETTLE_MS)
            {
                angle_ = wrapAngle(target_angle_);
                state_ = State::Idle;
                return true;
            }
            const float t = (float)elapsed / SETTLE_MS;
            const float distance = target_angle_ - start_angle_;
            const float tangent = settle_tangent_;
            angle_ = start_angle_ + distance * (3.0f*t*t - 2.0f*t*t*t)
                     + tangent * (t*t*t - 2.0f*t*t + t);
        }
        return false;
    }

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
        bool isFlashing = (frame.flashRemainingMs > 0);
        bool isDimmed = (!isSpinning && frame.targetFace == 1);
        const uint32_t flashIntensity = (uint32_t)clampInt(frame.flashRemainingMs, 0,
            max(1, frame.flashDurationMs)) * 256 / max(1, frame.flashDurationMs);

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
                    color = tint565(color, frame.flashColor, flashIntensity);
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
