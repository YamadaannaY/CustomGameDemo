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
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Phase1);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Phase2);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_BurstReady);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_LightAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_HeavyAttack);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_HeavyAttackRelease);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Skill);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Ultimate);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Dodge);
// 空中下砸（GA_AirAttack）专属输入。与 InputTag.LightAttack 平级而非其子级：
// AbilityTriggers 走层级匹配，做成子 tag 会把空中连打 GA 一起触发
UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_AirDive);

// 连击 / 闪避 内部事件 Tag
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Change_End);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_LastSection);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combo_HeavyTransition);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Evade_ToSprint);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Juhe_PhaseEnd);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Juhe);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_JuheReady);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ForwardOvershoot);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Cancel);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Push_Self);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Uninterruptible);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Uninterruptible_End);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(HeavyAttack_Shoot);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(Area_Damage);

// 空中攻击斩出剑气：Montage 的挥刀帧放 AN 触发一次，GA 每收到一次生成一道剑气
UE_DECLARE_GAMEPLAY_TAG_EXTERN(AirAttack_SwordQi);

// CancelWindow：后摇段「视为该 GA 已取消」的开关与开/关窗事件
UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_CancelWindow);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(CancelWindow_Begin);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(CancelWindow_End);

// 攻击连段「可衔接窗口」：Montage 上的 AN_AttackComboWindow 在区间首尾各发一次，
// 窗口内收到攻击输入才允许推进下一段，窗口外输入一律丢弃（不做输入缓存）
UE_DECLARE_GAMEPLAY_TAG_EXTERN(AirAttack_Combo_Begin);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(AirAttack_Combo_End);

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
	// 形态状态 Tag：GA_Burst01 等"第一形态专属大招"以它做 ActivationRequiredTags 门控，
	// 对应武器组把 State.Phase1 放进 AdditionalTags，切到第二形态(Phase2 组)后 tag 移除→大招自然失效
	static FGameplayTag GetPhase1StateTag();
	// 第二形态状态 Tag：切到 Phase2 武器组后该 tag 在 ASC 上；
	// 二阶段重击 GA 以它做 ActivationRequiredTags 门控，Character 长按判定也据此分支
	static FGameplayTag GetPhase2StateTag();

	// 大招解锁状态 Tag：一次「满足条件(打满 EnergyValue)」的重击成功激活时置位；
	// GA_Burst01 以它做 ActivationRequiredTags 门控，激活时消费移除 → 需重新满段重击才能再放大招。
	static FGameplayTag GetBurstReadyTag();
	static FGameplayTag GetSkill01Tag();
	static FGameplayTag GetBurst01Tag();
	static FGameplayTag GetBurstChangeStateTag();
	// ── 输入触发 Tag（废弃 InputID，改用 AbilityTriggers + GameplayEvent）──
	// 武器组 IA 全局固定，换武器只换背后 GA；GA 通过 AbilityTriggers 声明响应哪个 InputTag
	static FGameplayTag GetLightAttackInputTag();  // "InputTag.LightAttack"
	static FGameplayTag GetHeavyAttackInputTag();  // "InputTag.HeavyAttack"
	// 重击松手 Tag：二阶段蓄力重击 GA 监听，收到即打出结束段。
	// 注意：不可作为 "InputTag.HeavyAttack" 的子 tag——GA 的 AbilityTriggers 走层级匹配，子 tag 会误触发重击 GA 自身
	static FGameplayTag GetHeavyAttackReleaseInputTag();  // "InputTag.HeavyAttackRelease"
	static FGameplayTag GetSkillInputTag();        // "InputTag.Skill"
	static FGameplayTag GetUltimateInputTag();     // "InputTag.Ultimate"
	static FGameplayTag GetDodgeInputTag();        // "InputTag.Dodge"
	// 空中下砸触发 Tag：一阶段空中轻击、以及二阶段空中连打 GA 交接到第三段时都发它
	static FGameplayTag GetAirDiveInputTag();      // "InputTag.AirDive"

	// 连击 / 闪避内部事件 Tag
	static FGameplayTag GetComboChangedEventTag();      // "ability.combo.change"
	static FGameplayTag GetComboChangedEventEndTag();   // "ability.combo.change.end"
	static FGameplayTag GetComboTargetEventTag();       // "ability.combo.damage"
	static FGameplayTag GetComboLastSectionTag();       // "ability.combo.lastsection"
	static FGameplayTag GetComboHeavyTransitionTag();   // "ability.combo.heavytransition"
	static FGameplayTag GetEvadeToSprintTag();          // "Evade.ToSprint"

	// 居合（第二形态闪避特化）内部 Tag
	static FGameplayTag GetJuhePhaseEndTag();           // "Juhe.PhaseEnd"     居合 Montage 后摇起始帧 AN 发的分界事件
	static FGameplayTag GetJuheStateTag();              // "State.Juhe"        居合进行中，挡住普攻 GA 激活
	static FGameplayTag GetJuheReadyStateTag();         // "State.JuheReady"   二阶段普攻进 Section 后的 3s 居合窗口
	static FGameplayTag GetForwardOvershootStateTag();  // "State.ForwardOvershoot" 穿透区间落点越过目标（供 ANS_CombatCamera 条件触发）

	static FGameplayTag GetAbilityCancelTag();          // "ability.cancel"
	static FGameplayTag GetPushSelfTag();               // "ability.push.self"
	static FGameplayTag GetUninterruptibleTag();        // "State.Uninterruptible"
	static FGameplayTag GetUninterruptibleEndTag();     // "ability.uninterruptible.end"

	// 伤害 SetByCaller Tag（GE 的伤害 Modifier 通过此 Tag 读取攻击者攻击力）
	static FGameplayTag GetDamageSetByCallerTag();      // "Data.Damage"

	// 通用武器碰撞伤害事件 Tag（武器扫描命中后发给 Owner，攻击 GA 基类统一监听）
	static FGameplayTag GetAbilityDamageEventTag();     // "ability.damage"

	// 范围伤害事件 Tag：启用 bEnableAreaDamage 的 GA 基类统一监听，
	// Montage 伤害帧用 AN_SendGameplayEvent 触发一次，做以角色为中心的范围判定
	static FGameplayTag GetAreaDamageTag();             // "ability.area.damage"

	// 重击弓射：Montage 内各放箭帧 AN 触发一次本事件，GA 每收到一次生成一支箭
	static FGameplayTag GetHeavyAttackShootTag();       // "ability.heavyattack.shoot"

	// 空中攻击剑气：Montage 挥刀帧 AN 触发一次，GA 每收到一次生成一道剑气
	static FGameplayTag GetAirAttackSwordQiTag();       // "ability.airattack.swordqi"

	// ── CancelWindow（后摇段：GA 视为已取消，任何输入都能打断此 Montage）──
	// AN_CancelWindow 在区间 Begin/End 各发一次事件，持有者据此撤销/恢复自身封锁并登记句柄；
	// 任何 GA 在 CommitAbility 时查询持有者，命中即取消它。移动录入打断仍走 GetAbilityCancelTag。
	static FGameplayTag GetCancelWindowStateTag();      // "State.CancelWindow"        窗口开启中（松散 tag，供查询/调试）
	static FGameplayTag GetCancelWindowBeginTag();      // "ability.cancelwindow.begin" 进窗（AN NotifyBegin）
	static FGameplayTag GetCancelWindowEndTag();        // "ability.cancelwindow.end"   出窗（AN NotifyEnd）

	// 攻击连段可衔接窗口（AN_AttackComboWindow 的 NotifyBegin / NotifyEnd）
	static FGameplayTag GetAirAttackComboBeginTag();    // "ability.airattack.combo.begin"
	static FGameplayTag GetAirAttackComboEndTag();      // "ability.airattack.combo.end"

	static FGameplayTag GetLaunchedAbilityActivationTag();
};
