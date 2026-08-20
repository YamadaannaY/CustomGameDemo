// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
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
	/**	GAS在服务端做的初始化操作
	 *	- 初始化属性集 
	 *	- 应用初始 GE 
	 *	- 授予天生 GA
	 **/ 
	void ServerSideInit();

	// UnPossess 时清理天生 GA 和 GE
	void RemoveInnateAbilities();
	
	// 角色级能力：全部以 INDEX_NONE 授予，触发方式由各 GA 自身的 AbilityTriggers（InputTag）决定。
	// eg: Dodge(GA_Evade)归这里，不可装卸，武器技能组(Combo/AirAttack等)归 WeaponData 的 GrantedAbilities，可装卸。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UExtraGameplayAbility>> InnateAbilities;

	// 初始化GE：ServerSideInit 时应用到自身
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UGameplayEffect>> InitEffects;

	// 属性集类（蓝图可覆盖为子类）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TSubclassOf<UExtraGameAttributeSet> AttributeSetClass;

private:
	
	//将属性集添加到ASC中
	void InitializeBaseAttribute();
	
	//将GE应用到ASC中
	void ApplyInitialEffects();
	
	//将GA注册到ASC中
	void GiveInitialAbilities();

	TArray<FGameplayAbilitySpecHandle> InnateAbilityHandles;
	TArray<FActiveGameplayEffectHandle> InnateEffectHandles;
};
