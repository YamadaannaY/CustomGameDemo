
#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "AN_AreaCheck.generated.h"

/**
 * 范围伤害 AN：在动画帧上发送「范围伤害」事件
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_AreaCheck : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.area.damage
	virtual FGameplayTag GetEventTag() const override;

	// 圆心来源。默认 Inherit：跟随 GA 配置
	UPROPERTY(EditAnywhere, Category = "Area Check")
	EAreaCenterMode CenterMode = EAreaCenterMode::Inherit;

	// 圆心相对角色位置的偏移（仅 XY 生效，Z 忽略）。仅 CenterMode=Owner 时可配置
	UPROPERTY(EditAnywhere, Category = "Area Check", meta = (EditCondition = "CenterMode == EAreaCenterMode::Owner"))
	FVector CenterOffset = FVector::ZeroVector;

	// 检测半径（cm）。<=0 表示沿用 GA 自身配置的 AreaDamageRadius
	UPROPERTY(EditAnywhere, Category = "Area Check")
	float Radius = 0.f;

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
