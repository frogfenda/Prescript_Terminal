/*
【模块职责】TM6605 震动业务封装。通过 BSP 访问触觉驱动，按全局震动开关和强度等级映射到 TM6605 内置效果。
【阅读提示】本文件只保留上层语义接口；具体效果号和 I2C 写入由 bsp_tm6605 负责。
*/
#include "sys/sys_haptic.h"
#include "sys/sys_config.h"
#include "bsp/bsp_tm6605.h"

SysHaptic sysHaptic;

void SysHaptic::begin()
{
    BSP::Tm6605::Begin();
}

namespace
{
    void playSemantic(BSP::Tm6605::SemanticEffect effect, bool force = false)
    {
        if (!sysConfig.haptic_enable)
            return;

        BSP::Tm6605::PlaySemantic(effect, sysConfig.haptic_intensity, force);
    }
}

void SysHaptic::playTick() { playSemantic(BSP::Tm6605::SemanticEffect::Tick); }
void SysHaptic::playConfirm() { playSemantic(BSP::Tm6605::SemanticEffect::Confirm); }
void SysHaptic::playBack() { playSemantic(BSP::Tm6605::SemanticEffect::Back); }
void SysHaptic::playCoinHeads() { playSemantic(BSP::Tm6605::SemanticEffect::CoinHeads); }
void SysHaptic::playCoinTails() { playSemantic(BSP::Tm6605::SemanticEffect::CoinTails); }
void SysHaptic::playAlert() { playSemantic(BSP::Tm6605::SemanticEffect::Alert); }

void SysHaptic::playDecodeSuccess()
{
    // TM6605 不支持一次写入多段队列，这里使用 BSP 映射的单个完成效果。
    playSemantic(BSP::Tm6605::SemanticEffect::DecodeDone, true);
}

void SysHaptic_Sleep()
{
    BSP::Tm6605::Sleep();
}

void SysHaptic_Wakeup()
{
    BSP::Tm6605::Wakeup();
}
