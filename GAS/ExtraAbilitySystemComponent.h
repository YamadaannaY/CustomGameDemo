// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponTypes.h"
#include "ExtraAbilitySystemComponent.generated.h"

class UExtraGameplayAbility;
class UGameplayEffect;
class UExtraGameAttributeSet;
struct FActiveGameplayEffectHandle;

UCLASS(ClassGroup = (GAS), meta = (BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	// 初始化属性集 → 应用初始 GE → 授予天生 GA
	void ServerSideInit();

	// UnPossess 时清理天生 GA 和 GE
	void RemoveInnateAbilities();

	// ── 天生能力配置（在角色蓝图的 ASC 组件上配置）─────────

	// 主动技能：InputID → GA 映射
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TMap<EWeaponAbilityInputID, TSubclassOf<UExtraGameplayAbility>> InnateActiveAbilities;

	// 被动技能：无需 InputID，通过 Tag/Event 触发
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UExtraGameplayAbility>> InnatePassiveAbilities;

	// 初始化 GE：ServerSideInit 时应用到自身
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UGameplayEffect>> InitEffects;

	// 属性集类（蓝图可覆盖为子类）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TSubclassOf<UExtraGameAttributeSet> AttributeSetClass;

private:
	void InitializeBaseAttribute();
	void ApplyInitialEffects();
	void GiveInitialAbilities();

	TArray<FGameplayAbilitySpecHandle> InnateAbilityHandles;
	TArray<FActiveGameplayEffectHandle> InnateEffectHandles;
};
