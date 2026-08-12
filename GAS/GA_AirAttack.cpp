#include "GA_AirAttack.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_AirAttack::UGA_AirAttack()
{
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());
}

void UGA_AirAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!K2_CommitAbility())
	{
		K2_EndAbility();
		return;
	}

	if (!AirAttackMontage)
	{
		K2_EndAbility();
		return;
	}

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, AirAttackMontage);
		Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageBlendOut);
		Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageCancelled);
		Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageBlendOut);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageCancelled);
		Task->ReadyForActivation();
	}
}

void UGA_AirAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_AirAttack::OnMontageBlendOut()
{
	K2_EndAbility();
}

void UGA_AirAttack::OnMontageCancelled()
{
	K2_EndAbility();
}
