// 文件：src/apps/app_gacha_stats.cpp
#include "app_menu_base.h"
#include "app_manager.h"
#include "sys/sys_audio.h"
#include "sys_haptic.h"
#include "sys_config.h"

class AppGachaStats : public AppMenuBase {
protected:
    int getMenuCount() override { return 8; } 
    
    const char *getTitle() override { return ">> 提取部数据库"; }
    
    const char *getItemText(int index) override {
        static char buf[64];
        float t = sysConfig.gacha_stats.total > 0 ? (float)sysConfig.gacha_stats.total : 1.0f; 
        
        if (index == 0) sprintf(buf, "总计提取: %d 次", sysConfig.gacha_stats.total);
        else if (index == 1) sprintf(buf, "★★★: %d (%.1f%%)", sysConfig.gacha_stats.s3, sysConfig.gacha_stats.s3 / t * 100.0f);
        else if (index == 2) sprintf(buf, "★★: %d (%.1f%%)", sysConfig.gacha_stats.s2, sysConfig.gacha_stats.s2 / t * 100.0f);
        else if (index == 3) sprintf(buf, "★: %d (%.1f%%)", sysConfig.gacha_stats.s1, sysConfig.gacha_stats.s1 / t * 100.0f);
        else if (index == 4) sprintf(buf, "[W] 瓦夜总计: %d", sysConfig.gacha_stats.w3 + sysConfig.gacha_stats.w2);
        else if (index == 5) sprintf(buf, "  - [W] ★★★: %d", sysConfig.gacha_stats.w3);
        else if (index == 6) sprintf(buf, "  - [W] ★★: %d", sysConfig.gacha_stats.w2);
        else if (index == 7) sprintf(buf, "[ 按下清除所有记录 ]");
        
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
            sysHaptic.playTick();
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