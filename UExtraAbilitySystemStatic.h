#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UExtraAbilitySystemStatic.generated.h"

// ── Native GameplayTag 声明（静态初始化，早于 CDO 构造，无需手动注册）──
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_BasicAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Dodge);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Airborne);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_AirAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_LightAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_HeavyAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Skill);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ultimate);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Dodge);

// 连击 / 闪避 内部事件 Tag
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change_End);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Evade_ToSprint);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Cancel);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Push_Self);

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UUExtraAbilitySystemStatic : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	static FGameplayTag GetBasicAttackAbilityTag();
	static FGameplayTag GetDodgeAbilityTag();
	static FGameplayTag GetAirborneTag();
	static FGameplayTag GetAirAttackTag();
	static FGameplayTag GetSkill01Tag();

	// ── 输入触发 Tag（废弃 InputID，改用 AbilityTriggers + GameplayEvent）──
	// 武器组 IA 全局固定，换武器只换背后 GA；GA 通过 AbilityTriggers 声明响应哪个 InputTag
	static FGameplayTag GetLightAttackInputTag();  // "InputTag.LightAttack"
	static FGameplayTag GetHeavyAttackInputTag();  // "InputTag.HeavyAttack"
	static FGameplayTag GetSkillInputTag();        // "InputTag.Skill"
	static FGameplayTag GetUltimateInputTag();     // "InputTag.Ultimate"
	static FGameplayTag GetDodgeInputTag();        // "InputTag.Dodge"

	// 连击 / 闪避内部事件 Tag
	static FGameplayTag GetComboChangedEventTag();      // "ability.combo.change"
	static FGameplayTag GetComboChangedEventEndTag();   // "ability.combo.change.end"
	static FGameplayTag GetComboTargetEventTag();       // "ability.combo.damage"
	static FGameplayTag GetEvadeToSprintTag();          // "Evade.ToSprint"
	static FGameplayTag GetAbilityCancelTag();          // "ability.cancel"
	static FGameplayTag GetPushSelfTag();               // "ability.push.self"
	
	
	static FGameplayTag GetLaunchedAbilityActivationTag();
};
