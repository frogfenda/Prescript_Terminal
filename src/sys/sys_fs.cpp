// 文件：src/sys/sys_fs.cpp
#include "sys_fs.h"

std::vector<String> sys_prescripts_zh;
std::vector<String> sys_prescripts_en;

void SysFS_Init() {
    if (!LittleFS.begin(true)) { 
        Serial.println("[SYS_FS] ERROR: LittleFS 挂载失败！");
        return;
    }
    if (!LittleFS.exists("/assets")) {
        LittleFS.mkdir("/assets");
    }
}

// 通用读取函数
void load_pool(const char* path, std::vector<String>& pool, const char* fallback) {
    pool.clear();
    File file = LittleFS.open(path, "r");
    if (!file) {
        pool.push_back(fallback);
        return;
    }
    while(file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) pool.push_back(line);
    }
    file.close();
}

void SysFS_Load_Prescripts() {
    // 【分别加载中文和英文 TXT 库】
    load_pool("/assets/prescripts_zh.txt", sys_prescripts_zh, "系统覆盖。请上传 prescripts_zh.txt");
    load_pool("/assets/prescripts_en.txt", sys_prescripts_en, "SYS OVERRIDE. PLEASE UPLOAD prescripts_en.txt.");
    
    Serial.printf("[SYS_FS] 数据库载入！中文: %d 条, 英文: %d 条\n", sys_prescripts_zh.size(), sys_prescripts_en.size());
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