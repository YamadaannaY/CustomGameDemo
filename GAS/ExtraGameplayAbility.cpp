#include "ExtraGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "Engine/World.h"
#include "TimerManager.h"

UExtraGameplayAbility::UExtraGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;
}

UAnimInstance* UExtraGameplayAbility::GetOwnerAnimInstance() const
{
	USkeletalMeshComponent* OwnerSkeletalMeshComp=GetOwningComponentFromActorInfo();
	if (OwnerSkeletalMeshComp)
	{
		return OwnerSkeletalMeshComp->GetAnimInstance();
	}
	return nullptr;
}

void UExtraGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	// 清理移动打断定时器
	if (MovementCheckTimerHandle.IsValid())
	{
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(MovementCheckTimerHandle);
		}
		MovementCheckTimerHandle.Invalidate();
	}

	// 若因移动输入结束，淡出当前 Montage
	if (bEndingFromMovement)
	{
		if (UAnimMontage* ActiveMontage = GetActiveMontageForCancel())
		{
			UAnimInstance* AnimInst = GetOwnerAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(ActiveMontage))
			{
				AnimInst->Montage_Stop(MovementCancelBlendOutTime, ActiveMontage);
			}
		}
	}

	AExtraPlayerCharacter* Char = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (Char && Char->GetWeaponComponent() && ClearWeaponShowOnAbilityEnd)
	{
		Char->GetWeaponComponent()->HideWeapon();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UExtraGameplayAbility::SetupMovementCancel()
{
	// 重置跨激活状态（InstancedPerActor 实例复用）
	bEndingFromMovement = false;
	if (MovementCheckTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(MovementCheckTimerHandle);
		MovementCheckTimerHandle.Invalidate();
	}

	UAbilityTask_WaitGameplayEvent* WaitCancelTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, GetMovementCancelTag(), nullptr, false, true);
	WaitCancelTask->EventReceived.AddDynamic(this, &ThisClass::OnMovementCancelNotifyReceived);
	WaitCancelTask->ReadyForActivation();
}

FGameplayTag UExtraGameplayAbility::GetMovementCancelTag() const
{
	return UUExtraAbilitySystemStatic::GetAbilityCancelTag();
}

void UExtraGameplayAbility::OnMovementCancelNotifyReceived(FGameplayEventData Payload)
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

void UExtraGameplayAbility::CheckMovementInputForCancel()
{
	if (HasMovementInput())
	{
		GetWorld()->GetTimerManager().ClearTimer(MovementCheckTimerHandle);
		bEndingFromMovement = true;
		K2_EndAbility();
	}
}

bool UExtraGameplayAbility::HasMovementInput() const
{
	// 用角色的 bHasMoveInput（输入级信号），而非 GetCurrentAcceleration（物理级，会因空中/落地硬直/时序滞后而为 0）
	AExtraPlayerCharacter* AvatarChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (AvatarChar)
	{
		return AvatarChar->HasMoveInput();
	}

	return false;
}
