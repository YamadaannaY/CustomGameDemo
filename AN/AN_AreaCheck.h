// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_AreaCheck.generated.h"

/**
 * 范围检测 AN：在动画帧上发送「范围伤害」事件（固定 ability.area.damage），
 * 并附带 FAreaCheckData（圆心相对角色的 XY 偏移 + 半径），由 GA 基类解析后做半径判定。
 * 未填偏移/半径时行为与 AN_SendGameplayEvent 一致（角色中心 + GA 的 AreaDamageRadius）。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_AreaCheck : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.area.damage（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

	// 圆心相对角色位置的偏移（仅 XY 生效，Z 忽略）
	UPROPERTY(EditAnywhere, Category = "Area Check")
	FVector CenterOffset = FVector::ZeroVector;

	// 检测半径（cm）。<=0 表示沿用 GA 自身配置的 AreaDamageRadius
	UPROPERTY(EditAnywhere, Category = "Area Check")
	float Radius = 0.f;

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
