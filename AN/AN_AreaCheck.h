// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_AreaCheck.generated.h"

/**
 * 范围检测 AN：在动画帧上发送「范围伤害」事件，
 * 并附带 FAreaCheckData（圆心来源 + 偏移 + 半径），由 GA 解析后做半径判定。
 * 半径未指定时沿用 GA 默认的 AreaDamageRadius。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_AreaCheck : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.area.damage（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

	// 圆心来源。默认 Inherit：跟随 GA 配置；也可在本 AN 上强制指定（如本段以锁定目标为中心）
	UPROPERTY(EditAnywhere, Category = "Area Check")
	EAreaCenterMode CenterMode = EAreaCenterMode::Inherit;

	// 圆心相对角色位置的偏移（仅 XY 生效，Z 忽略）。仅 CenterMode=Owner 时使用
	UPROPERTY(EditAnywhere, Category = "Area Check", meta = (EditCondition = "CenterMode == EAreaCenterMode::Owner", EditConditionHides))
	FVector CenterOffset = FVector::ZeroVector;

	// 检测半径（cm）。<=0 表示沿用 GA 自身配置的 AreaDamageRadius
	UPROPERTY(EditAnywhere, Category = "Area Check")
	float Radius = 0.f;

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
