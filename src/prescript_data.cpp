// 文件：src/prescript_data.cpp
#include "prescript_data.h"

// ==========================================
// 英文都市指令库 (English Prescripts)
// ==========================================
static const char* prescripts_en[] = {
    "Look at the sky for 10 seconds.",
    "Drink a glass of water immediately.",
    "Take 3 deep breaths right now.",
    "Smile to the nearest person.",
    "Stand up and stretch your arms."
};

// ==========================================
// 中文都市指令库 (Chinese Prescripts)
// ==========================================
// 【重要提示】：由于目前的排版动画算法是按 20 个字节硬切的。
// 一个汉字占 3 个字节，如果长句子不加空格，硬切时可能会把汉字劈成两半导致乱码。
// 临时解决方案：在中文长句中加入空格，引导算法在空格处安全换行。
static const char* prescripts_zh[] = {
    "凝视天空 十秒钟",
    "立刻 去喝一杯水",
    "现在进行 三次深呼吸",
    "对距离最近的人 微笑",
    "起立, 并伸展你的双臂就是这个样子的秒,就是这个样子的秒,就是这个样子的喵,就是这个样子的喵"
};

int Get_Prescript_Count(SystemLang_t lang) {
    if (lang == LANG_ZH) {
        return sizeof(prescripts_zh) / sizeof(prescripts_zh[0]);
    } else {
        return sizeof(prescripts_en) / sizeof(prescripts_en[0]);
    }
}

const char* Get_Prescript(SystemLang_t lang, int index) {
    if (lang == LANG_ZH) {
        return prescripts_zh[index];
    } else {
        return prescripts_en[index];
    }
}