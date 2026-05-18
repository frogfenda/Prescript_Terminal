/*
【模块职责】硬币应用的绘制与布局辅助。App 层只传入硬币状态，UI 层负责根据当前屏幕尺寸计算硬币大小、位置，并把贴图缩放绘制到缓冲区。
【设计说明】这个模块不管理预设、不计算结果、不保存配置；这些业务仍保留在 app_coin_flip.cpp，避免硬币应用拆分过度。
*/
#pragma once

#include <Arduino.h>
#include "sys/sys_constants.h"

namespace UICoin
{
    // 当前 428×142 横向屏幕下的显示策略。
    // 素材建议后续换成 96×96；运行显示按数量分档，而不是每多一枚就重新计算尺寸。前几个档位放大，增强单枚/少量硬币的视觉存在感。
    constexpr int COIN_RENDER_MIN = 38;
    // 后续硬币素材按 96×96 单规格准备；运行时最大也按 96px 绘制。
    // 这里仍保留缩放能力，是为了同一素材在 6~18 枚时按档位缩小，
    // 不是为了同时适配多套不同尺寸素材。
    constexpr int COIN_RENDER_MAX_QUICK = 96;
    constexpr int COIN_RENDER_MAX_SKILL = 90;
    constexpr int COIN_RENDER_BUFFER_SIZE = COIN_RENDER_MAX_QUICK;
    constexpr int MAX_COINS = PrescriptConst::MAX_COIN_COUNT;
    constexpr int MAX_COLS_PER_ROW = 9;

    struct StageLayout
    {
        int rows = 1;
        int cols = 1;
        int coinSize = 64;
        int x[MAX_COINS] = {0};
        int y[MAX_COINS] = {0};
    };

    struct CoinFrame
    {
        float scaleX = 1.0f;      // 翻转压缩比例。正数用 heads，负数用 tails。
        bool isFlipping = false;  // 是否仍在翻转。
        int targetFace = 0;       // 0 正面，1 反面。
        int flashFrames = 0;      // 正面落定后的闪光剩余帧。
        int flashDuration = 6;    // 闪光总帧数。
        int material = 0;         // 0 金，1 红，2 绿。
    };

    /**
     * 根据硬币数量和可用区域计算硬币舞台布局。
     * 规则：一行最多 9 枚，最多 18 枚；硬币尺寸按数量分档，当前档位塞得下就不继续缩小。
     */
    StageLayout BuildStageLayout(int coinCount, int areaX, int areaY, int areaW, int areaH, bool skillMode);

    /**
     * 把一枚硬币缩放绘制到外部缓冲区。
     * bufferSize 必须不小于 targetSize，推荐使用 COIN_RENDER_BUFFER_SIZE。
     */
    bool DrawCoinToBuffer(uint16_t *buffer, int bufferSize, const CoinFrame &frame, int targetSize);
}
