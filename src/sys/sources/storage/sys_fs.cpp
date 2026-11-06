/*
【模块职责】LittleFS 文件工具实现。锁定语言固件只加载当前语言指令池；运行时语言版同时加载中英文池；新格式不存在时由内容库迁移旧TXT。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/sys/storage/sys_fs.cpp
#include "sys/storage/sys_fs.h"
#include "lang/terminal_lang.h"
#include "sys/resources/sys_prescript_catalog.h"


void SysFS_Init() {
    /*
     * LittleFS保存配置、待机图和普通指令，挂载失败不等于分区内容可以丢弃。
     * formatOnFail=false保证分区异常、分区表迁移或瞬时Flash错误时保留现场；
     * 后续只能通过明确的维护/重刷流程恢复，不能在普通启动过程中静默格式化用户数据。
     */
    if (!LittleFS.begin(false)) {
        Serial.println("[文件系统] LittleFS挂载失败，已保留原始分区，未自动格式化。");
        return;
    }
    if (!LittleFS.exists("/common")) LittleFS.mkdir("/common");
    if (!LittleFS.exists("/zh")) LittleFS.mkdir("/zh");
    if (!LittleFS.exists("/en")) LittleFS.mkdir("/en");
    // 旧目录只作为迁移兜底，不主动创建；这样新设备的文件系统结构更干净。
}

// 内容库负责新JSON与旧TXT迁移，文件工具不再维护第二份可变正文池。
void SysFS_Load_Prescripts() {
    SysPrescriptCatalog::Load();
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
