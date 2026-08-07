// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "ExtraGameplayAbility.generated.h"

/**
 * 自定义 GA 基类
 *
 * 所有武器技能 GA 蓝图的父类应设为此类。
 * InputID 由武器组件按数组索引自动分配（无需在蓝图中手动设置）：
 *   [0] LightAttack  [1] HeavyAttack  [2] Skill  [3] Ultimate  [4] Dodge
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	UExtraGameplayAbility();
};
