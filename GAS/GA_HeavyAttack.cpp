#include "GA_HeavyAttack.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"


UGA_HeavyAttack::UGA_HeavyAttack()
{
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackTag());
	
	FAbilityTriggerData HeavyAttackTrigger;
	HeavyAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	HeavyAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetHeavyAttackInputTag();
	AbilityTriggers.Add(HeavyAttackTrigger);
}

void UGA_HeavyAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,HeavyAttackMontage);
		MontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->ReadyForActivation();
	}
}

void UGA_HeavyAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
