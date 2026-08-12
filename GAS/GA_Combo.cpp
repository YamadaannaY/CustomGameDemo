#include "GA_Combo.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayTagsManager.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include  "GameFramework/CharacterMovementComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Combo::UGA_Combo() : ComboMontage(nullptr)
{
	//为了在GA内不重复触发GA，且在空中时不激活地面 Combo
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());
}

void UGA_Combo::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (! K2_CommitAbility())
	{
		K2_EndAbility();
		return ;
	}

	if (HasAuthorityOrPredictionKey(ActorInfo,&ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* PlayComboMontageTask=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,ComboMontage);
		PlayComboMontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->ReadyForActivation();

		//接收Notify的EventTag
		UAbilityTask_WaitGameplayEvent* WaitComboChangeEventTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GetComboChangedEventTag(),nullptr,false,false);
		WaitComboChangeEventTask->EventReceived.AddDynamic(this,&ThisClass::ComboChangedEventReceived);
		WaitComboChangeEventTask->ReadyForActivation();

		UAbilityTask_WaitGameplayEvent* WaitCancelAbilityTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GetRecoveryCancelTag(),nullptr,false,true);
		WaitCancelAbilityTask->EventReceived.AddDynamic(this,&ThisClass::OnRecoveryCancelNotifyReceived);
		WaitCancelAbilityTask->ReadyForActivation();

	}

	//在服务端实现Damage逻辑
	if (K2_HasAuthority())
	{
		UAbilityTask_WaitGameplayEvent* WaitTargetEventTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GetComboTargetEventTag());
		WaitTargetEventTask->EventReceived.AddDynamic(this,&ThisClass::DoDamage);
		WaitTargetEventTask->ReadyForActivation();

	}

	//处理第一次输入
	SetupWaitComboInputPress();
}

void UGA_Combo::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (MovementCheckTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(MovementCheckTimerHandle);
	}

	if (bEndingFromMovement && ComboMontage)
	{
		UAnimInstance* AnimInst = GetOwnerAnimInstance();
		if (AnimInst && AnimInst->Montage_IsPlaying(ComboMontage))
		{
			AnimInst->Montage_Stop(MovementCancelBlendOutTime, ComboMontage);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

FGameplayTag UGA_Combo::GetComboChangedEventTag()
{
	return FGameplayTag::RequestGameplayTag("ability.combo.change");
}

FGameplayTag UGA_Combo::GetComboChangedEventEndTag()
{
	return FGameplayTag::RequestGameplayTag("ability.combo.change.end");
}

FGameplayTag UGA_Combo::GetComboTargetEventTag()
{
	return FGameplayTag::RequestGameplayTag("ability.combo.damage");
}

FGameplayTag UGA_Combo::GetRecoveryCancelTag()
{
	return FGameplayTag::RequestGameplayTag("ability.combo.cancel");
}

void UGA_Combo::HandleInputPress(float TimeWaited)
{
	SetupWaitComboInputPress();
	TryCommitCombo();
}

void UGA_Combo::SetupWaitComboInputPress()
{
	UAbilityTask_WaitInputPress* WaitInputPress=UAbilityTask_WaitInputPress::WaitInputPress(this);
	WaitInputPress->OnPress.AddDynamic(this,&ThisClass::HandleInputPress);
	WaitInputPress->ReadyForActivation();
}

void UGA_Combo::TryCommitCombo()
{
	if (NextComboName==NAME_None) return;

	UAnimInstance* OwnerAnimInst=GetOwnerAnimInstance();
	if (!OwnerAnimInst) return;

	//设置当前Montage Section的NextSection
	OwnerAnimInst->Montage_SetNextSection(OwnerAnimInst->Montage_GetCurrentSection(ComboMontage),NextComboName,ComboMontage);
	OwnerAnimInst->Montage_JumpToSection(NextComboName, ComboMontage);

	NextComboName=NAME_None;
	bEndingFromMovement = false;
}

TSubclassOf<UGameplayEffect> UGA_Combo::GetDamageEffectForCurrentCombo() const
{
	if (UAnimInstance* OwnerAnimInst=GetOwnerAnimInstance())
	{
		const FName CurrentSectionName=OwnerAnimInst->Montage_GetCurrentSection(ComboMontage);
		const TSubclassOf<UGameplayEffect>* FoundEffectPtr=DamageEffectMap.Find(CurrentSectionName);
		if (FoundEffectPtr)
		{
			return *FoundEffectPtr;
		}
	}
	return DefaultDamageEffect;
}

void UGA_Combo::ComboChangedEventReceived(FGameplayEventData InPayLoad)
{
	const FGameplayTag EventTag=InPayLoad.EventTag;

	if (EventTag==GetComboChangedEventEndTag())
	{
		//此时到Section末尾，需要重置NextComboName
		NextComboName=NAME_None;
		return;
	}

	TArray<FName> TagNames;
	UGameplayTagsManager::Get().SplitGameplayTagFName(EventTag, TagNames);
	NextComboName=TagNames.Last();
}

void UGA_Combo::DoDamage(FGameplayEventData Data)
{
}

void UGA_Combo::OnRecoveryCancelNotifyReceived(FGameplayEventData Payload)
{
	if (HasMovementInput())
	{
		bEndingFromMovement = true;
		K2_EndAbility();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		MovementCheckTimerHandle,
		this,
		&ThisClass::CheckMovementInputForCancel,
		0.1f,
		true
	);
}

bool UGA_Combo::HasMovementInput() const
{
	AExtraPlayerCharacter* AvatarChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (AvatarChar && AvatarChar->GetCharacterMovement())
	{
		return AvatarChar->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared() > 0.0f;
	}

	return false  ;
}

void UGA_Combo::CheckMovementInputForCancel()
{
	if (HasMovementInput())
	{
		GetWorld()->GetTimerManager().ClearTimer(MovementCheckTimerHandle);
		bEndingFromMovement = true;
		K2_EndAbility();
	}
}