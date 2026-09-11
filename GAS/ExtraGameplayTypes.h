#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "ExtraGameplayTypes.generated.h"

// 携带「推力」参数的 TargetData：AN_ApplyPush 通过 GameplayEvent 把推力向量 + 覆盖标志进行发送
USTRUCT()
struct FPushTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	FVector PushVelocity = FVector::ZeroVector;
	bool bOverrideXY = false;
	bool bOverrideZ = true;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};

// 携带「范围检测」参数的 TargetData：AN_AreaCheck 通过 GameplayEvent 把圆心偏移 + 半径发给 GA
USTRUCT()
struct FAreaCheckData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	// 圆心相对角色位置的偏移（仅 XY 生效，Z 忽略、沿用角色高度）
	FVector CenterOffset = FVector::ZeroVector;

	// 检测半径（cm）。<=0 表示沿用 GA 自身配置的 AreaDamageRadius
	float Radius = 0.f;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
};
