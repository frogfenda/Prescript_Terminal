/*
【模块职责】LittleFS 文件工具实现。锁定语言固件只加载当前语言指令池；运行时语言版同时加载中英文池；缺失文件时写入默认提示文本。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/sys_fs.cpp
#include "sys_fs.h"
#include "../lang/terminal_lang.h"

std::vector<String> sys_prescripts_zh;
std::vector<String> sys_prescripts_en;

void SysFS_Init() {
    if (!LittleFS.begin(true)) { 
        Serial.println("[SYS_FS] ERROR: LittleFS 挂载失败！");
        return;
    }
    if (!LittleFS.exists("/common")) LittleFS.mkdir("/common");
    if (!LittleFS.exists("/zh")) LittleFS.mkdir("/zh");
    if (!LittleFS.exists("/en")) LittleFS.mkdir("/en");
    // 旧目录只作为迁移兜底，不主动创建；这样新设备的文件系统结构更干净。
}

// 通用读取函数
void load_pool_from_path(const char* path, std::vector<String>& pool, bool clear_first) {
    if (clear_first)
        pool.clear();
    File file = LittleFS.open(path, "r");
    if (!file)
        return;
    while(file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) pool.push_back(line);
    }
    file.close();
}

// 【函数说明】按新路径优先加载指令池；旧 /assets 路径只作过渡兜底，方便尚未重刷 LittleFS 的开发板启动。
void load_pool(SystemLang_t lang, std::vector<String>& pool) {
    pool.clear();
    load_pool_from_path(TerminalLang::PrescriptPath(lang), pool, false);
    if (pool.empty())
        load_pool_from_path(TerminalLang::LegacyPrescriptPath(lang), pool, false);
    if (pool.empty())
        pool.push_back(TerminalLang::PrescriptFallback(lang));
}

void SysFS_Load_Prescripts() {
    // Stage 6B：目录结构保持兼容，但编译期锁定版只加载当前语言，避免双语资源常驻内存。
    sys_prescripts_zh.clear();
    sys_prescripts_en.clear();

    if (TerminalLang::LOCKED)
    {
        SystemLang_t lang = TerminalLang::DEFAULT_LANG;
        if (lang == LANG_ZH)
        {
            load_pool(LANG_ZH, sys_prescripts_zh);
            Serial.printf("[SYS_FS] 中文锁定版数据库载入：中文 %d 条，英文库跳过。\n", (int)sys_prescripts_zh.size());
        }
        else
        {
            load_pool(LANG_EN, sys_prescripts_en);
            Serial.printf("[SYS_FS] EN locked DB loaded: EN %d records, ZH pool skipped.\n", (int)sys_prescripts_en.size());
        }
        return;
    }

    // 运行时语言切换版保持旧行为：同时加载双语库。
    load_pool(LANG_ZH, sys_prescripts_zh);
    load_pool(LANG_EN, sys_prescripts_en);
    Serial.printf("[SYS_FS] 数据库载入！中文: %d 条, 英文: %d 条\n", (int)sys_prescripts_zh.size(), (int)sys_prescripts_en.size());
}

String SysFS_Read_File(const char* filepath) {
    if (!LittleFS.exists(filepath)) return "";
    File file = LittleFS.open(filepath, "r");
    if (!file) return "";
    String content = file.readString();
    file.close();
    return content;
}

bool SysFS_Write_File(const char* filepath, const char* content) {
    File file = LittleFS.open(filepath, "w");
    if (!file) return false;
    file.print(content);
    file.close();
    return true;
}

bool SysFS_Append_File(const char* filepath, const char* content) {
    File file = LittleFS.open(filepath, "a");
    if (!file) return false;
    file.println(content);
    file.close();
    return true;
}
