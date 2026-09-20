#include "UExtraAbilitySystemStatic.h"

// ── Native GameplayTag 定义（静态注册，早于 CDO 构造）──
UE_DEFINE_GAMEPLAY_TAG(Ability, "ability");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack, "ability.basicattack");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_AirAttack, "ability.basicattack.airattack");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_Light, "ability.basicattack.light");
UE_DEFINE_GAMEPLAY_TAG(Ability_BasicAttack_Heavy, "ability.basicattack.heavy");
UE_DEFINE_GAMEPLAY_TAG(Ability_Dodge, "ability.dodge");
UE_DEFINE_GAMEPLAY_TAG(Ability_Skill_01, "ability.Skill.01");
UE_DEFINE_GAMEPLAY_TAG(Ability_Burst_01, "ability.Burst.01");
UE_DEFINE_GAMEPLAY_TAG(Ability_Burst_Changestate, "ability.Burst.changestate");
UE_DEFINE_GAMEPLAY_TAG(State_Airborne, "character.state.airborne");
UE_DEFINE_GAMEPLAY_TAG(State_Phase1, "State.Phase1");
UE_DEFINE_GAMEPLAY_TAG(State_Phase2, "State.Phase2");
UE_DEFINE_GAMEPLAY_TAG(State_BurstReady, "State.BurstReady");
UE_DEFINE_GAMEPLAY_TAG(InputTag_LightAttack, "InputTag.LightAttack");
UE_DEFINE_GAMEPLAY_TAG(InputTag_HeavyAttack, "InputTag.HeavyAttack");
UE_DEFINE_GAMEPLAY_TAG(InputTag_HeavyAttackRelease, "InputTag.HeavyAttackRelease");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Skill, "InputTag.Skill");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Ultimate, "InputTag.Ultimate");
UE_DEFINE_GAMEPLAY_TAG(InputTag_Dodge, "InputTag.Dodge");
UE_DEFINE_GAMEPLAY_TAG(InputTag_AirDive, "InputTag.AirDive");
UE_DEFINE_GAMEPLAY_TAG(InputTag_AttackPro, "InputTag.AttackPro");
UE_DEFINE_GAMEPLAY_TAG(Combo_Change, "ability.combo.change");
UE_DEFINE_GAMEPLAY_TAG(Combo_Change_End, "ability.combo.change.end");
UE_DEFINE_GAMEPLAY_TAG(Combo_Damage, "ability.combo.damage");
UE_DEFINE_GAMEPLAY_TAG(Ability_Damage, "ability.damage");
UE_DEFINE_GAMEPLAY_TAG(Combo_LastSection, "ability.combo.lastsection");
UE_DEFINE_GAMEPLAY_TAG(Combo_HeavyTransition, "ability.combo.heavytransition");
UE_DEFINE_GAMEPLAY_TAG(Evade_ToSprint, "Evade.ToSprint");
UE_DEFINE_GAMEPLAY_TAG(Juhe_PhaseEnd, "Juhe.PhaseEnd");
UE_DEFINE_GAMEPLAY_TAG(State_Juhe, "State.Juhe");
UE_DEFINE_GAMEPLAY_TAG(State_JuheReady, "State.JuheReady");
UE_DEFINE_GAMEPLAY_TAG(State_ForwardOvershoot, "State.ForwardOvershoot");
UE_DEFINE_GAMEPLAY_TAG(Ability_Cancel, "ability.cancel");
UE_DEFINE_GAMEPLAY_TAG(Push_Self, "ability.push.self");
UE_DEFINE_GAMEPLAY_TAG(Ability_Passive_Launch, "ability.passive.launch.activate");
UE_DEFINE_GAMEPLAY_TAG(State_Uninterruptible, "State.Uninterruptible");
UE_DEFINE_GAMEPLAY_TAG(Uninterruptible_End, "ability.uninterruptible.end");
UE_DEFINE_GAMEPLAY_TAG(Data_Damage, "Data.Damage");
UE_DEFINE_GAMEPLAY_TAG(HeavyAttack_Shoot, "ability.heavyattack.shoot");
UE_DEFINE_GAMEPLAY_TAG(Area_Damage, "ability.area.damage");
UE_DEFINE_GAMEPLAY_TAG(AirAttack_SwordQi, "ability.airattack.swordqi");
UE_DEFINE_GAMEPLAY_TAG(Ability_AttackPro_Phase2, "ability.AttackPro.Phase2");
UE_DEFINE_GAMEPLAY_TAG(State_ProJuheCount, "State.ProJuheCount");
UE_DEFINE_GAMEPLAY_TAG(State_ProReady, "State.ProReady");

// CancelWindow：注意 "ability.cancelwindow.*" 不是 "ability.cancel" 的子级（同前缀但不同分支），
// 事件按 tag 层级向上广播，因此不会误触发移动打断的监听。
UE_DEFINE_GAMEPLAY_TAG(State_CancelWindow, "State.CancelWindow");
UE_DEFINE_GAMEPLAY_TAG(CancelWindow_Begin, "ability.cancelwindow.begin");
UE_DEFINE_GAMEPLAY_TAG(CancelWindow_End, "ability.cancelwindow.end");

// 连段可衔接窗口（二阶段空中连斩等）：与 "ability.basicattack.airattack" 不同分支，互不误触发
UE_DEFINE_GAMEPLAY_TAG(AirAttack_Combo_Begin, "ability.airattack.combo.begin");
UE_DEFINE_GAMEPLAY_TAG(AirAttack_Combo_End, "ability.airattack.combo.end");

FGameplayTag UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag()
{
	return Ability_BasicAttack_Light;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAbilityTag()
{
	return Ability;
}

FGameplayTag UUExtraAbilitySystemStatic::GetDodgeAbilityTag()
{
	return Ability_Dodge;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirborneTag()
{
	return State_Airborne;
}

FGameplayTag UUExtraAbilitySystemStatic::GetPhase1StateTag()
{
	return State_Phase1;
}

FGameplayTag UUExtraAbilitySystemStatic::GetPhase2StateTag()
{
	return State_Phase2;
}

FGameplayTag UUExtraAbilitySystemStatic::GetBurstReadyTag()
{
	return State_BurstReady;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirAttackAbilityTag()
{
	return Ability_BasicAttack_AirAttack;
}

FGameplayTag UUExtraAbilitySystemStatic::GetSkill01Tag()
{
	return Ability_Skill_01;
}

FGameplayTag UUExtraAbilitySystemStatic::GetBurst01Tag()
{
	return Ability_Burst_01;
}

FGameplayTag UUExtraAbilitySystemStatic::GetBurstChangeStateTag()
{
	return Ability_Burst_Changestate;
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

FGameplayTag UUExtraAbilitySystemStatic::GetHeavyAttackReleaseInputTag()
{
	return InputTag_HeavyAttackRelease;
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

FGameplayTag UUExtraAbilitySystemStatic::GetAirDiveInputTag()
{
	return InputTag_AirDive;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAttackProInputTag()
{
	return InputTag_AttackPro;
}

FGameplayTag UUExtraAbilitySystemStatic::GetProJuheCountTag()
{
	return State_ProJuheCount;
}

FGameplayTag UUExtraAbilitySystemStatic::GetProReadyTag()
{
	return State_ProReady;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAttackProAbilityTag()
{
	return Ability_AttackPro_Phase2;
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

FGameplayTag UUExtraAbilitySystemStatic::GetJuhePhaseEndTag()
{
	return Juhe_PhaseEnd;
}

FGameplayTag UUExtraAbilitySystemStatic::GetJuheStateTag()
{
	return State_Juhe;
}

FGameplayTag UUExtraAbilitySystemStatic::GetJuheReadyStateTag()
{
	return State_JuheReady;
}

FGameplayTag UUExtraAbilitySystemStatic::GetForwardOvershootStateTag()
{
	return State_ForwardOvershoot;
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

FGameplayTag UUExtraAbilitySystemStatic::GetAbilityDamageEventTag()
{
	return Ability_Damage;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAreaDamageTag()
{
	return Area_Damage;
}

FGameplayTag UUExtraAbilitySystemStatic::GetHeavyAttackShootTag()
{
	return HeavyAttack_Shoot;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirAttackSwordQiTag()
{
	return AirAttack_SwordQi;
}

FGameplayTag UUExtraAbilitySystemStatic::GetLaunchedAbilityActivationTag()
{
	return Ability_Passive_Launch ;
}

FGameplayTag UUExtraAbilitySystemStatic::GetCancelWindowStateTag()
{
	return State_CancelWindow;
}

FGameplayTag UUExtraAbilitySystemStatic::GetCancelWindowBeginTag()
{
	return CancelWindow_Begin;
}

FGameplayTag UUExtraAbilitySystemStatic::GetCancelWindowEndTag()
{
	return CancelWindow_End;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirAttackComboBeginTag()
{
	return AirAttack_Combo_Begin;
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirAttackComboEndTag()
{
	return AirAttack_Combo_End;
}
