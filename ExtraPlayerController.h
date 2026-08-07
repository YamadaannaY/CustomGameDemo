// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayAbilitySpecHandle.h"
#include "WeaponSystem/ExtraGameWeaponTypes.h"
#include "ExtraPlayerController.generated.h"

struct FActiveGameplayEffectHandle;
class UExtraGameplayAbility;
class UGameplayEffect;

/**
 * 玩家控制器
 * 持有天生能力配置（仅玩家角色需要，怪物不需要），
 * 在 Possess 时授予给 pawn 的 ASC。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:
	// ── 天生能力配置（在控制器蓝图中自由配置）─────────────────

	// 主动技能：InputID → GA 映射（如 Dodge(6) → GA_Dodge）
	// OnPossess 时授予，OnUnPossess 时移除
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TMap<EWeaponAbilityInputID, TSubclassOf<UExtraGameplayAbility>> InnateActiveAbilities;

	// 被动技能：无需 InputID，通过 Tag/Event 触发
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UExtraGameplayAbility>> InnatePassiveAbilities;

	// 初始化 GE：Possess 时应用到自身，无限持续
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UGameplayEffect>> InitEffects;

private:
	// 已授予的天生 GA Handle（UnPossess 时清理）
	TArray<FGameplayAbilitySpecHandle> InnateAbilityHandles;

	// 已应用的天生 GE Handle（UnPossess 时清理）
	TArray<FActiveGameplayEffectHandle> InnateEffectHandles;

	// 授予/清理天生能力
	void GrantInnateAbilities(class UAbilitySystemComponent* ASC);
	void RemoveInnateAbilities(class UAbilitySystemComponent* ASC);
};
