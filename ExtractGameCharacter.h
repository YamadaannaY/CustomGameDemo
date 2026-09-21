
#pragma once

#include "CoreMinimal.h"

// 相机臂探测专用通道。必须与武器扫描通道（ECC_GameTraceChannel1）分开：
// 角色胶囊需要对相机臂 Ignore（否则紧贴时臂长被敌方胶囊压到命中点，相机怼到角色身前），
// 但它必须对武器扫描保持 Block，两者共用一个通道时无法分别配置。
#define ECC_SpringArm ECC_GameTraceChannel3
#define ECC_Target ECC_GameTraceChannel2

