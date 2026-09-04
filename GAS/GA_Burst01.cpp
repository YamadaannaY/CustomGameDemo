// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_Burst01.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Burst01::UGA_Burst01()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetBurst01Tag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAbilityTag());
	
	FAbilityTriggerData BurstTrigger;
	BurstTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	BurstTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetUltimateInputTag();	
	AbilityTriggers.Add(BurstTrigger);
	
	bEnableUninterruptible = true;
}

void UGA_Burst01::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogTemp,Warning,TEXT("[GA_Burst01] : The GA has been activated"));
	
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,BurstMontage);
	MontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->ReadyForActivation();
	
	
	USkeletalMeshComponent* MeshComp = Cast<AExtraCharacter>(GetAvatarActorFromActorInfo())->GetMesh();
	if (MeshComp)
	{
		for (const auto& pair : SlotMaterialMap)
		{
			MeshComp->SetMaterialByName(pair.Key,pair.Value);
		}
	}
}

bool UGA_Burst01::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	/*if (SlotMaterialMap.Num()==0) return false ; 
	if (!BurstMontage) return false ; */
	
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UGA_Burst01::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                             const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
