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
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_LightAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_HeavyAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Skill);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ultimate);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Dodge);

// 连击 / 闪避 内部事件 Tag
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change_End);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_LastSection);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_HeavyTransition);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Evade_ToSprint);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Cancel);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Push_Self);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Uninterruptible);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Uninterruptible_End);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(HeavyAttack_Shoot);

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UUExtraAbilitySystemStatic : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	static FGameplayTag GetAbilityTag();
	static FGameplayTag GetBasicAttackAbilityTag();
	static FGameplayTag GetHeavyAttackAbilityTag();
	static FGameplayTag GetAirAttackAbilityTag();
	static FGameplayTag GetDodgeAbilityTag();
	static FGameplayTag GetAirborneTag();
	static FGameplayTag GetSkill01Tag();
	static FGameplayTag GetBurst01Tag();

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
	static FGameplayTag GetComboLastSectionTag();       // "ability.combo.lastsection"
	static FGameplayTag GetComboHeavyTransitionTag();   // "ability.combo.heavytransition"
	static FGameplayTag GetEvadeToSprintTag();          // "Evade.ToSprint"
	static FGameplayTag GetAbilityCancelTag();          // "ability.cancel"
	static FGameplayTag GetPushSelfTag();               // "ability.push.self"
	static FGameplayTag GetUninterruptibleTag();        // "State.Uninterruptible"
	static FGameplayTag GetUninterruptibleEndTag();     // "ability.uninterruptible.end"

	// 伤害 SetByCaller Tag（GE 的伤害 Modifier 通过此 Tag 读取攻击者攻击力）
	static FGameplayTag GetDamageSetByCallerTag();      // "Data.Damage"

	// 通用武器碰撞伤害事件 Tag（武器扫描命中后发给 Owner，攻击 GA 基类统一监听）
	static FGameplayTag GetAbilityDamageEventTag();     // "ability.damage"

	// 重击弓射：Montage 内各放箭帧 AN 触发一次本事件，GA 每收到一次生成一支箭
	static FGameplayTag GetHeavyAttackShootTag();       // "ability.heavyattack.shoot"

	static FGameplayTag GetLaunchedAbilityActivationTag();
};
