#include "GA_Skill_01.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Skill_01::UGA_Skill_01()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	bEnableUninterruptible = true;

	bEnableWeaponDamage = true;

	bRotateToLockTarget = true;

	InputTag = UUExtraAbilitySystemStatic::GetSkillInputTag();
}

void UGA_Skill_01::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		// 本技能释放后，下一次普攻连段从最后一段起并额外充能
		if (AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
		{
			PlayerCharacter->MarkSkill01ComboBoost();
		}

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
