#include "UExtraAbilitySystemStatic.h"

// ── Native GameplayTag 定义（静态注册，早于 CDO 构造）──
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack, "ability.basicattack");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_AirAttack, "ability.basicattack.airattack");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_Light, "ability.basicattack.light");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_Heavy, "ability.basicattack.heavy");
UE_DEFINE_GAMEPLAY_TAG(Ability_Dodge, "ability.dodge");
UE_DEFINE_GAMEPLAY_TAG(Ability_Skill_01, "ability.Skill.01");
UE_DEFINE_GAMEPLAY_TAG(State_Airborne, "character.state.airborne");
UE_DEFINE_GAMEPLAY_TAG(InputTag_LightAttack, "InputTag.LightAttack");
UE_DEFINE_GAMEPLAY_TAG(InputTag_HeavyAttack, "InputTag.HeavyAttack");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Skill, "InputTag.Skill");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Ultimate, "InputTag.Ultimate");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Dodge, "InputTag.Dodge");
UE_DEFINE_GAMEPLAY_TAG(Combo_Change, "ability.combo.change");
UE_DEFINE_GAMEPLAY_TAG(Combo_Change_End, "ability.combo.change.end");
UE_DEFINE_GAMEPLAY_TAG(Combo_Damage, "ability.combo.damage");
UE_DEFINE_GAMEPLAY_TAG(Combo_Commit, "ability.combo.commit");
UE_DEFINE_GAMEPLAY_TAG(Combo_LastSection, "ability.combo.lastsection");
UE_DEFINE_GAMEPLAY_TAG(Combo_HeavyTransition, "ability.combo.heavytransition");
UE_DEFINE_GAMEPLAY_TAG(Evade_ToSprint, "Evade.ToSprint");
UE_DEFINE_GAMEPLAY_TAG(Ability_Cancel, "ability.cancel");
UE_DEFINE_GAMEPLAY_TAG(Push_Self, "ability.push.self");
UE_DEFINE_GAMEPLAY_TAG(Ability_Passive_Launch, "ability.passive.launch.activate");
UE_DEFINE_GAMEPLAY_TAG(State_Uninterruptible, "State.Uninterruptible");
UE_DEFINE_GAMEPLAY_TAG(Uninterruptible_End, "ability.uninterruptible.end");
UE_DEFINE_GAMEPLAY_TAG(Data_Damage, "Data.Damage");

FGameplayTag UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag()
{
	return Ability_BasicAttack_Light;
}

FGameplayTag UUExtraAbilitySystemStatic::GetDodgeAbilityTag()
{
	return Ability_Dodge;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirborneTag()
{
	return State_Airborne;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirAttackAbilityTag()
{
	return Ability_BasicAttack_AirAttack;
}

FGameplayTag UUExtraAbilitySystemStatic::GetSkill01Tag()
{
	return Ability_Skill_01;
}

FGameplayTag UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag()
{
	return Ability_BasicAttack_Heavy;
}

FGameplayTag UUExtraAbilitySystemStatic::GetLightAttackInputTag()
{
	return InputTag_LightAttack;
}

FGameplayTag UUExtraAbilitySystemStatic::GetHeavyAttackInputTag()
{
	return InputTag_HeavyAttack;
}

FGameplayTag UUExtraAbilitySystemStatic::GetSkillInputTag()
{
	return InputTag_Skill;
}

FGameplayTag UUExtraAbilitySystemStatic::GetUltimateInputTag()
{
	return InputTag_Ultimate;
}

FGameplayTag UUExtraAbilitySystemStatic::GetDodgeInputTag()
{
	return InputTag_Dodge;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboChangedEventTag()
{
	return Combo_Change;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboChangedEventEndTag()
{
	return Combo_Change_End;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboTargetEventTag()
{
	return Combo_Damage;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboCommitEventTag()
{
	return Combo_Commit;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboLastSectionTag()
{
	return Combo_LastSection;
}

FGameplayTag UUExtraAbilitySystemStatic::GetComboHeavyTransitionTag()
{
	return Combo_HeavyTransition;
}

FGameplayTag UUExtraAbilitySystemStatic::GetEvadeToSprintTag()
{
	return Evade_ToSprint;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAbilityCancelTag()
{
	return Ability_Cancel;
}

FGameplayTag UUExtraAbilitySystemStatic::GetPushSelfTag()
{
	return Push_Self;
}

FGameplayTag UUExtraAbilitySystemStatic::GetUninterruptibleTag()
{
	return State_Uninterruptible;
}

FGameplayTag UUExtraAbilitySystemStatic::GetUninterruptibleEndTag()
{
	return Uninterruptible_End;
}

FGameplayTag UUExtraAbilitySystemStatic::GetDamageSetByCallerTag()
{
	return Data_Damage;
}

FGameplayTag UUExtraAbilitySystemStatic::GetLaunchedAbilityActivationTag()
{
	return Ability_Passive_Launch ; 
}
