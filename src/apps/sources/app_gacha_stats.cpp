/*
【模块职责】抽卡统计页。用菜单查看总抽数、各星级、瓦夜统计，并支持清空统计。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
// 文件：src/apps/app_gacha_stats.cpp
#include "apps/app_menu_base.h"
#include "sys/app_manager.h"
#include "sys/sys_audio.h"
#include "sys/sys_haptic.h"
#include "sys/sys_config.h"
#include "lang/ui_strings.h"

class AppGachaStats : public AppMenuBase {
protected:
    int getMenuCount() override { return 8; } 
    
    const char *getTitle() override { return UIStrings::GachaStatsTitle(appManager.getLanguage()); }
    
    const char *getItemText(int index) override {
        static char buf[64];
        SystemLang_t lang = appManager.getLanguage();
        float t = sysConfig.gacha_stats.total > 0 ? (float)sysConfig.gacha_stats.total : 1.0f; 
        
        if (index == 0) snprintf(buf, sizeof(buf), "%s: %d %s", UIStrings::GachaTotalLabel(lang), sysConfig.gacha_stats.total, UIStrings::GachaTotalUnit(lang));
        else if (index == 1) sprintf(buf, "★★★: %d (%.1f%%)", sysConfig.gacha_stats.s3, sysConfig.gacha_stats.s3 / t * 100.0f);
        else if (index == 2) sprintf(buf, "★★: %d (%.1f%%)", sysConfig.gacha_stats.s2, sysConfig.gacha_stats.s2 / t * 100.0f);
        else if (index == 3) sprintf(buf, "★: %d (%.1f%%)", sysConfig.gacha_stats.s1, sysConfig.gacha_stats.s1 / t * 100.0f);
        else if (index == 4) snprintf(buf, sizeof(buf), "%s: %d", UIStrings::GachaWalpurgisLabel(lang), sysConfig.gacha_stats.w3 + sysConfig.gacha_stats.w2);
        else if (index == 5) sprintf(buf, "  - [W] ★★★: %d", sysConfig.gacha_stats.w3);
        else if (index == 6) sprintf(buf, "  - [W] ★★: %d", sysConfig.gacha_stats.w2);
        else if (index == 7) snprintf(buf, sizeof(buf), "%s", UIStrings::GachaClearItem(lang));
        
        return buf;
    }

    uint16_t getItemColor(int index) override {
        if (index == 1) return TFT_GOLD;
        if (index == 2) return TFT_RED;
        if (index == 3) return TFT_DARKGREY;
        if (index >= 4 && index <= 6) return TFT_GREEN;
        if (index == 7) return TFT_RED; 
        return TFT_WHITE; 
    }

    void onItemClicked(int index) override {
        if (index == 7) { 
            SYS_SOUND_GLITCH();  
            Feedback_PlayKnobTick();
            // 一键清空并写盘
            sysConfig.gacha_stats.total = 0;
            sysConfig.gacha_stats.s3 = 0; sysConfig.gacha_stats.s2 = 0; sysConfig.gacha_stats.s1 = 0;
            sysConfig.gacha_stats.w3 = 0; sysConfig.gacha_stats.w2 = 0;
            sysConfig.save(); 
        }
    }

    void onLongPressed() override {
        SYS_SOUND_NAV();
        appManager.popApp();
    }
};

AppGachaStats instanceGachaStats;
AppBase *appGachaStats = &instanceGachaStats;
