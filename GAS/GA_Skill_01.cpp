#include "GA_Skill_01.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Skill_01::UGA_Skill_01()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	// 窗口：表现动画段挂 State.Uninterruptible，后摇段由 AN_EndUninterruptible 放开。
	bEnableUninterruptible = true;

	// 启用通用武器碰撞伤害（基类机制）：服务端监听命中事件并应用 DefaultDamageEffect
	bEnableWeaponDamage = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

	FAbilityTriggerData SkillTrigger;
	SkillTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	SkillTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetSkillInputTag();
	AbilityTriggers.Add(SkillTrigger);
}

void UGA_Skill_01::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,Skill01Montage);
		MontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->ReadyForActivation();
	}
}

void UGA_Skill_01::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
