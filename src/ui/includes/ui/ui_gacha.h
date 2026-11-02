/*
【模块职责】十连碎镜演出的几何缓存和逐帧绘制，不依赖身份目录、概率、音效或文件系统。
【调用约定】进入待抽页 Start 一次生成布局，DrawIdle 保持无色轻晃；按键抽取后 SetPulls 绑定结果，
每帧先清 Sprite，翻面阶段先画原有结果页，再 Draw 覆盖镜面。
Draw 不清屏、不推屏、不分配内存。全部调用只发生在 UI 主循环，实例与 App 一起保存。
*/
#pragma once
#include <Arduino.h>

namespace UIGacha
{
    constexpr int PULL_COUNT = 10;
    // 短按后只留 80ms 起势；后续扫光、保持、翻面仍沿用原来的各段速度。
    constexpr uint32_t SWEEP_START_MS = 80;
    constexpr uint32_t SWEEP_END_MS = SWEEP_START_MS + 1400;
    constexpr uint32_t FLIP_START_MS = SWEEP_END_MS + 510;
    constexpr uint32_t DURATION_MS = FLIP_START_MS + 440;

    // 仅复制表现所需信息；star=0 表示无有效记录，walp 沿用结算页绿色语义。
    struct PullAppearance
    {
        uint8_t star = 0;
        bool walp = false;
    };

    namespace Detail
    {
        // 十点分区最多 13 边，斜切/旋转交集最多 27 边，投影再次求交最多 54 边；
        // 反光带再增加两条裁面，容量留到 64，避免罕见种子被截断。不使用堆或整屏副本。
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

    class MirrorAnimation
    {
    public:
        /** 复制十条外观摘要并生成本次布局。视觉种子只驱动局部伪随机数，不消耗抽卡随机序列。 */
        void Start(uint32_t seed, const PullAppearance (&pulls)[PULL_COUNT], int width, int height);

        /** 仅更新十条结果摘要，不重新生成布局；待抽镜面与扫光演出因此保持同一组碎镜。 */
        void SetPulls(const PullAppearance (&pulls)[PULL_COUNT]);

        /** 待抽状态：motion_ms 是进入待抽页以来的毫秒数；始终无色、不扫光、不翻面。 */
        void DrawIdle(uint32_t motion_ms) const;

        /** 开始抽取后的毫秒时间；可重复、倒序采样。达到 DURATION_MS 后不写任何像素。 */
        void Draw(uint32_t elapsed_ms) const;

        /** elapsed_ms 控制扫光/翻面，motion_ms 延续待抽晃动时钟，按下开始时镜面不会跳位。 */
        void Draw(uint32_t elapsed_ms, uint32_t motion_ms) const;

    private:
        void DrawFrame(uint32_t elapsed_ms, uint32_t motion_ms, bool idle) const;
        Detail::Shard shards_[PULL_COUNT] = {};
        int width_ = 0, height_ = 0;
    };
}
