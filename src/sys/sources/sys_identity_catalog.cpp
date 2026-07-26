/*
【模块职责】解析Gacha身份JSON、在PSRAM构造String对象并维护三个星级索引。
【安全边界】加载使用“先构建新池、成功后替换旧池”，App通过值复制抽取，不跨语言持有内部地址。
*/
#include "sys/sys_identity_catalog.h"

#include "sys/sys_resource_io.h"
#include <ArduinoJson.h>
#include <new>

namespace
{
    IdentityData *g_pool = nullptr;
    int g_total = 0;
    int *g_starIndexes[3] = {nullptr, nullptr, nullptr};
    int g_starCounts[3] = {0, 0, 0};
    SystemLang_t g_loadedLang = LANG_ZH;
    bool g_loaded = false;

    void ReleasePool()
    {
        if (g_pool)
        {
            for (int i = 0; i < g_total; ++i)
                g_pool[i].~IdentityData();
            free(g_pool);
        }
        for (uint8_t i = 0; i < 3; ++i)
        {
            free(g_starIndexes[i]);
            g_starIndexes[i] = nullptr;
            g_starCounts[i] = 0;
        }
        g_pool = nullptr;
        g_total = 0;
        g_loaded = false;
    }

    int StarSlot(uint8_t star)
    {
        return star >= 1 && star <= 3 ? (int)star - 1 : -1;
    }
}

bool SysIdentityCatalog::Load(SystemLang_t lang)
{
    if (g_loaded && g_loadedLang == lang && g_total > 0)
        return true;

    fs::File file;
    const SysResourcePath path = {TerminalLang::IdsPath(lang)};
    String resolvedPath;
    if (!SysResourceIO::OpenRead(path, file, "提取部身份", &resolvedPath))
    {
        ReleasePool();
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, file);
    file.close();
    if (error || !document.is<JsonArray>() || document.size() == 0)
    {
        ReleasePool();
        Serial.printf("[身份目录] JSON解析失败或为空：%s，错误=%s。\n",
                      resolvedPath.c_str(), error ? error.c_str() : "根节点不是非空数组");
        return false;
    }

    const int total = (int)document.size();
    IdentityData *newPool = (IdentityData *)ps_malloc(sizeof(IdentityData) * total);
    int *newIndexes[3] = {
        (int *)ps_malloc(sizeof(int) * total),
        (int *)ps_malloc(sizeof(int) * total),
        (int *)ps_malloc(sizeof(int) * total)};
    if (!newPool || !newIndexes[0] || !newIndexes[1] || !newIndexes[2])
    {
        free(newPool);
        for (uint8_t i = 0; i < 3; ++i)
            free(newIndexes[i]);
        ReleasePool();
        Serial.printf("[身份目录] 申请PSRAM失败：记录=%d。\n", total);
        return false;
    }

    int newCounts[3] = {0, 0, 0};
    for (int i = 0; i < total; ++i)
    {
        new (&newPool[i]) IdentityData();
        newPool[i].sinner = document[i]["sinner"].as<String>();
        newPool[i].id_name = document[i]["id"].as<String>();
        newPool[i].star = document[i]["star"].as<int>();
        newPool[i].walp = document[i]["walp"].as<int>();
        const int slot = StarSlot((uint8_t)newPool[i].star);
        if (slot >= 0)
            newIndexes[slot][newCounts[slot]++] = i;
    }

    ReleasePool();
    g_pool = newPool;
    g_total = total;
    for (uint8_t i = 0; i < 3; ++i)
    {
        g_starIndexes[i] = newIndexes[i];
        g_starCounts[i] = newCounts[i];
    }
    g_loadedLang = lang;
    g_loaded = true;
    Serial.printf("[身份目录] 已预加载%s身份：%d条。\n",
                  TerminalLang::DisplayName(lang, LANG_ZH), total);
    return true;
}

bool SysIdentityCatalog::HasStar(uint8_t star)
{
    const int slot = StarSlot(star);
    return slot >= 0 && g_starCounts[slot] > 0;
}

bool SysIdentityCatalog::DrawByStar(uint8_t star, IdentityData &out)
{
    const int slot = StarSlot(star);
    if (slot < 0 || !g_pool || g_starCounts[slot] <= 0)
        return false;
    const int poolIndex = g_starIndexes[slot][random(g_starCounts[slot])];
    if (poolIndex < 0 || poolIndex >= g_total)
        return false;
    out = g_pool[poolIndex];
    return true;
}

int SysIdentityCatalog::Count()
{
    return g_total;
}
