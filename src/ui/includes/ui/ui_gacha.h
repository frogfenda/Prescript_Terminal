/*
【模块职责】十连的散落碎镜/行驶车窗两套演出，共用几何缓存和稀有度配色。
【调用约定】进入待抽页 Start 一次选择样式并生成布局，DrawIdle 保持轻晃/行驶；按键后 SetPulls 绑定结果，
每帧先清 Sprite，翻面阶段先画原有结果页，再 Draw 覆盖镜面。
Draw 不清屏、不推屏、不分配内存。全部调用只发生在 UI 主循环，实例与 App 一起保存。
*/
#pragma once
#include <Arduino.h>

namespace UIGacha
{
    constexpr int PULL_COUNT = 10;
    // 短按后 60ms 起光，保留完整扫光与翻面，把长保持段压短；总长 1910ms。
    constexpr uint32_t SWEEP_START_MS = 60;
    constexpr uint32_t SWEEP_END_MS = SWEEP_START_MS + 1100;
    constexpr uint32_t FLIP_START_MS = SWEEP_END_MS + 350;
    constexpr uint32_t DURATION_MS = FLIP_START_MS + 400;
    // 车窗完整时间轴加快 4/3，保留各段相对节奏；街景运动时钟不受提取加速影响。
    constexpr uint32_t BUS_FLIP_START_MS = 1725;
    constexpr uint32_t BUS_DURATION_MS = 2100;

    // App 每次进入待抽页选一次；UI 不自行随机切换，也不接触抽卡业务。
    enum class AnimationStyle : uint8_t { MirrorShards, BusWindow };

    // 仅复制表现所需信息；star=0 表示无有效记录，walp 沿用结算页绿色语义。
    struct PullAppearance
    {
        uint8_t star = 0;
        bool walp = false;
    };

    namespace Detail
    {
        // 十点分区最多 13 边，斜切/旋转交集最多 27 边，投影再次求交最多 54 边；
        // 容量留到 64，避免罕见种子被截断；材质按扫描线分色，不再额外构造反光带多边形。
        constexpr int MAX_VERTICES = 64;
        struct Point { float x, y; };
        struct Polygon { Point points[MAX_VERTICES]; int count = 0; };
        struct Shard
        {
            Polygon cell;
            Point origin;
            float axis, phase;
            PullAppearance appearance;
        };
    }

    class ExtractionAnimation
    {
    public:
        /** 复制十条外观摘要并生成本次布局。视觉种子只驱动局部伪随机数，不消耗抽卡随机序列。 */
        void Start(uint32_t seed, const PullAppearance (&pulls)[PULL_COUNT], int width, int height,
                   AnimationStyle style = AnimationStyle::MirrorShards);

        /** App 使用当前样式的翻面/结束时刻，避免两个演出共用错误的硬编码时长。 */
        uint32_t FlipStartMs() const { return style_ == AnimationStyle::BusWindow ? BUS_FLIP_START_MS : FLIP_START_MS; }
        uint32_t DurationMs() const { return style_ == AnimationStyle::BusWindow ? BUS_DURATION_MS : DURATION_MS; }

        /** 仅更新十条结果摘要，不重新生成布局；待抽镜面与扫光演出因此保持同一组碎镜。 */
        void SetPulls(const PullAppearance (&pulls)[PULL_COUNT]);

        /** 待抽状态：motion_ms 是进入待抽页以来的毫秒数；碎镜轻晃或巴士行驶，不泄露稀有度。 */
        void DrawIdle(uint32_t motion_ms) const;

        /** 开始抽取后的毫秒时间；可重复、倒序采样。达到 DurationMs() 后不写任何像素。 */
        void Draw(uint32_t elapsed_ms) const;

        /** elapsed_ms 控制扫光/翻面，motion_ms 延续待抽晃动时钟，按下开始时镜面不会跳位。 */
        void Draw(uint32_t elapsed_ms, uint32_t motion_ms) const;

    private:
        void DrawMirrorFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const;
        void DrawBusFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const;
        Detail::Shard shards_[PULL_COUNT] = {};
        int width_ = 0, height_ = 0;
        AnimationStyle style_ = AnimationStyle::MirrorShards;
    };
}
