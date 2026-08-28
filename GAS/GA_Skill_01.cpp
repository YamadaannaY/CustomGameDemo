#include "GA_Skill_01.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Skill_01::UGA_Skill_01()
{
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	// 窗口：表现动画段挂 State.Uninterruptible，后摇段由 AN_EndUninterruptible 放开。
	bEnableUninterruptible = true;

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
