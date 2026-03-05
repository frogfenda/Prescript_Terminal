// 文件：src/prescript_logic.cpp
#include "prescript_logic.h"
#include "prescript_data.h"
#include <Arduino.h>

const char* Prescript_GetRandomRule(void) {
    // 纯粹的数据提取逻辑
    int index = random(Get_Prescript_Count());
    return Get_Prescript(index);
}