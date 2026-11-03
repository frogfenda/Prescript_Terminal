/*
【模块职责】十连的散落碎镜/行驶车窗演出与打印式结果清单，共用稀有度语义。
【调用约定】进入待抽页 Start 一次选择样式并生成布局，DrawIdle 保持轻晃/行驶；按键后 SetPulls 绑定结果，
每帧先清 Sprite，翻面阶段先画正式打印清单，再由演出覆盖镜面。
演出不分配内存；清单使用 UIText 的临时短字符串，不额外分配整屏缓冲。均不清屏、不推屏。
全部调用只发生在 UI 主循环，实例与 App 一起保存。
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

    // 仅在 Draw 调用期间借用 App 的身份字符串；UI 不保留目录指针、不修改抽取结果。
    struct ReceiptRecord
    {
        const char *sinner = "";
        const char *identity = "";
        PullAppearance appearance;
    };

    /**
     * 打印清单只保存十条记录的运动位置，不持有文本或额外整屏缓冲。
     * Reset 在新十连完成分配时调用；Select 合并旋钮输入，连续插值到新选中行。
     * Draw 的 entry_ms 是从翻面开始计的纸面进场时间；跳过演出时传 ENTRY_MS 即直达完整清单。
     * now_ms 与 Select 使用同一 millis 时钟；所有接口只供 UI 主循环使用，不清屏、不推屏。
     */
    class ResultReceipt
    {
    public:
        static constexpr uint32_t ENTRY_MS = 480;
        static constexpr uint32_t SELECT_MS = 160;
        void Reset(int selected, uint32_t now_ms);
        void Select(int selected, uint32_t now_ms);
        int Selected() const { return selected_; }
        bool IsMoving(uint32_t now_ms) const;
        // 保留末帧请求直到 Draw 真正提交稳定位置，避免低帧率时停在插值的倒数一帧。
        bool NeedsFrame() const { return moving_ || printing_; }
        // 最后一次 Draw 检测出的长名宽度供主循环决定是否继续刷新；短名静止后停止刷屏。
        bool HasLongDetail() const { return detail_overflow_ > 0; }
        void Draw(const ReceiptRecord (&records)[PULL_COUNT], const char *title,
                  int width, int height, uint32_t entry_ms, uint32_t now_ms);

    private:
        float RowPosition(int row, uint32_t now_ms) const;
        float Expansion(int row, uint32_t now_ms) const;
        float from_position_[PULL_COUNT] = {};
        float from_expansion_[PULL_COUNT] = {};
        int selected_ = 0;
        int detail_overflow_ = 0;
        uint32_t selection_time_ = 0;
        uint32_t detail_time_ = 0;
        bool moving_ = false;
        bool printing_ = false;
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
