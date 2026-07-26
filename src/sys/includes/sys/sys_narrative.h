/*
【模块职责】加载并保存由“场景→段落→句子”组成的只读叙事素材，提供带权随机场景选择。
【能力边界】本模块只管理文本、RGB565 颜色和音频绑定标识，不绘制 UI、不播放音频、不推进 App 交互状态。
【资源约束】JSON 通过统一资源IO从已挂载的FATFS读取，解析结果保存在固定上限的vector中；异常或超量条目会被忽略。
*/
#pragma once

#include <Arduino.h>
#include <vector>

struct SysNarrativeLine
{
    String text;
    String audioBind;
};

struct SysNarrativeParagraph
{
    uint16_t color = 0xFFFF;
    std::vector<SysNarrativeLine> lines;
};

struct SysNarrativeScene
{
    String id;
    uint16_t weight = 1;
    std::vector<SysNarrativeParagraph> paragraphs;
};

class SysNarrativeCatalog
{
public:
    /**
     * 【接口说明】从 FATFS JSON 加载一份叙事目录；同一路径已经成功加载时直接复用缓存。
     * 【返回值】至少得到一个包含有效句子的场景时返回 true；失败时清空旧目录并返回 false。
     * 【线程约束】只能从 Arduino 主循环/App 生命周期调用，不能从音频、网络任务或中断调用。
     */
    bool load(const char *path);

    /** 清空全部 String/vector 资源；通常只在切换语言或重新加载失败时调用。 */
    void clear();

    /**
     * 按 weight 随机选择场景。场景数大于一时会排除 avoidIndex，避免连续重复；失败返回 -1。
     */
    int chooseWeightedScene(int avoidIndex = -1) const;

    /** 返回稳定的只读场景指针；下次 load()/clear() 后旧指针失效。非法索引返回 nullptr。 */
    const SysNarrativeScene *scene(int index) const;

    int sceneCount() const { return (int)scenes_.size(); }
    bool empty() const { return scenes_.empty(); }

private:
    String loaded_path_;
    std::vector<SysNarrativeScene> scenes_;
};
