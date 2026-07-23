/*
【模块职责】实现通用叙事 JSON 的有界解析与带权随机选择。
【JSON 结构】根节点 scenes[]；每个 scene 含 id/weight/paragraphs[]；段落含 color/lines[]；
每个 line 含 text 和可选 audio。audio 只是稳定标识，实际播放由使用该目录的 App/音频绑定层决定。
*/
#include "sys/sys_narrative.h"

#include <ArduinoJson.h>
#include <FFat.h>
#include <string.h>
#include <utility>

namespace
{
    constexpr size_t MAX_SCENES = 24;
    constexpr size_t MAX_PARAGRAPHS_PER_SCENE = 12;
    constexpr size_t MAX_LINES_PER_PARAGRAPH = 24;
    constexpr size_t MAX_TEXT_BYTES = 768;
    constexpr size_t MAX_AUDIO_BIND_BYTES = 128;
    constexpr uint16_t DEFAULT_COLOR = 0xFFFF;
    constexpr uint16_t MAX_WEIGHT = 10000;

    uint16_t ParseColor(JsonVariantConst value)
    {
        if (value.is<const char *>())
        {
            const char *text = value.as<const char *>();
            if (!text || text[0] == '\0')
                return DEFAULT_COLOR;
            return (uint16_t)strtoul(text, nullptr, 0);
        }
        if (value.is<uint16_t>() || value.is<unsigned int>() || value.is<int>())
            return (uint16_t)value.as<unsigned int>();
        return DEFAULT_COLOR;
    }

    bool IsBoundedText(const char *text, size_t maxBytes)
    {
        return text && text[0] != '\0' && strnlen(text, maxBytes + 1) <= maxBytes;
    }
}

void SysNarrativeCatalog::clear()
{
    scenes_.clear();
    loaded_path_ = "";
}

bool SysNarrativeCatalog::load(const char *path)
{
    if (!path || path[0] == '\0')
    {
        clear();
        return false;
    }
    if (loaded_path_ == path && !scenes_.empty())
        return true;

    clear();
    File file = FFat.open(path, FILE_READ);
    if (!file)
    {
        Serial.printf("[叙事] 素材加载失败：未找到 %s。\n", path);
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, file);
    file.close();
    if (error)
    {
        Serial.printf("[叙事] JSON 解析失败：%s，错误=%s。\n", path, error.c_str());
        return false;
    }

    JsonArrayConst sceneArray = document["scenes"].as<JsonArrayConst>();
    if (sceneArray.isNull())
    {
        Serial.printf("[叙事] JSON 缺少 scenes 数组：%s。\n", path);
        return false;
    }

    scenes_.reserve(min(sceneArray.size(), MAX_SCENES));
    for (JsonObjectConst sceneObject : sceneArray)
    {
        if (scenes_.size() >= MAX_SCENES)
            break;

        SysNarrativeScene scene;
        scene.id = sceneObject["id"] | "";
        const int rawWeight = sceneObject["weight"] | 1;
        scene.weight = (uint16_t)constrain(rawWeight, 1, (int)MAX_WEIGHT);

        JsonArrayConst paragraphArray = sceneObject["paragraphs"].as<JsonArrayConst>();
        for (JsonObjectConst paragraphObject : paragraphArray)
        {
            if (scene.paragraphs.size() >= MAX_PARAGRAPHS_PER_SCENE)
                break;

            SysNarrativeParagraph paragraph;
            paragraph.color = ParseColor(paragraphObject["color"]);

            JsonArrayConst lineArray = paragraphObject["lines"].as<JsonArrayConst>();
            for (JsonObjectConst lineObject : lineArray)
            {
                if (paragraph.lines.size() >= MAX_LINES_PER_PARAGRAPH)
                    break;

                const char *text = lineObject["text"] | "";
                const char *audio = lineObject["audio"] | "";
                if (!IsBoundedText(text, MAX_TEXT_BYTES))
                    continue;
                if (audio && strnlen(audio, MAX_AUDIO_BIND_BYTES + 1) > MAX_AUDIO_BIND_BYTES)
                    audio = "";

                SysNarrativeLine line;
                line.text = text;
                line.audioBind = audio ? audio : "";
                paragraph.lines.push_back(std::move(line));
            }

            if (!paragraph.lines.empty())
                scene.paragraphs.push_back(std::move(paragraph));
        }

        if (!scene.paragraphs.empty())
        {
            if (scene.id.length() == 0)
                scene.id = "scene_" + String((unsigned)scenes_.size());
            scenes_.push_back(std::move(scene));
        }
    }

    if (scenes_.empty())
    {
        Serial.printf("[叙事] 素材中没有有效场景：%s。\n", path);
        return false;
    }

    loaded_path_ = path;
    Serial.printf("[叙事] 已加载 %d 个场景：%s。\n", (int)scenes_.size(), path);
    return true;
}

int SysNarrativeCatalog::chooseWeightedScene(int avoidIndex) const
{
    if (scenes_.empty())
        return -1;
    if (scenes_.size() == 1)
        return 0;

    uint32_t totalWeight = 0;
    for (size_t i = 0; i < scenes_.size(); ++i)
    {
        if ((int)i != avoidIndex)
            totalWeight += scenes_[i].weight;
    }
    if (totalWeight == 0)
        return avoidIndex == 0 ? 1 : 0;

    uint32_t roll = (uint32_t)random((long)totalWeight);
    for (size_t i = 0; i < scenes_.size(); ++i)
    {
        if ((int)i == avoidIndex)
            continue;
        if (roll < scenes_[i].weight)
            return (int)i;
        roll -= scenes_[i].weight;
    }
    return avoidIndex == 0 ? 1 : 0;
}

const SysNarrativeScene *SysNarrativeCatalog::scene(int index) const
{
    if (index < 0 || index >= (int)scenes_.size())
        return nullptr;
    return &scenes_[(size_t)index];
}
