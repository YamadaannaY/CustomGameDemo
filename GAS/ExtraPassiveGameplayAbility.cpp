// Fill out your copyright notice in the Description page of Project Settings.


#include "ExtraPassiveGameplayAbility.h"

UExtraPassiveGameplayAbility::UExtraPassiveGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UExtraPassiveGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 门控交给 ActivationBlockedTags（霸体等 tag 在激活前就被拒），这里只做转发
	OnReactActivated(TriggerEventData);
}
