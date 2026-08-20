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

	// 若因移动输入结束，用 Montage 资产自身配置的 BlendOut 时长停止（而非 0s 硬停），避免动画跳变。
	// 位移已通过 RootMotion 开关 AN / LayerPerBone 混合与姿势解耦，过渡期间即可移动，无需靠 0s 抢输入。
	if (bEndingFromMovement)
	{
		if (UAnimMontage* ActiveMontage = GetActiveMontageForCancel())
		{
			UAnimInstance* AnimInst = GetOwnerAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(ActiveMontage))
			{
				AnimInst->Montage_Stop(MontageCancelBlendOutTime, ActiveMontage);
			}
		}
	}

	AExtraPlayerCharacter* Char = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (Char && Char->GetWeaponComponent() && ClearWeaponShowOnAbilityEnd)
	{
		Char->GetWeaponComponent()->HideWeapon();
	}

	// 恢复 RootMotionMode：AN_ToggleRootMotion 可能已切到 IgnoreRootMotion，
	// 若 GA 被打断时没有对应 AN 去恢复，会全局残留导致后续所有带 root motion 的 montage 失效。
	// 统一在 GA 结束时强制回默认（Montage 专用 root motion），兜底兜全。
	// 注意：RootMotionMode 属于 UAnimInstance，而非 CharacterMovementComponent。
	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		AnimInst->RootMotionMode = ERootMotionMode::RootMotionFromEverything;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UExtraGameplayAbility::PreActivate(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData)
{
	Super::PreActivate(Handle, ActorInfo, ActivationInfo, OnGameplayAbilityEndedDelegate, TriggerEventData);

	// 移动打断：无需子类在 ActivateAbility 里手动挂载，只要 bEnableMovementCancel 为 true 即在此统一监听。
	if (bEnableMovementCancel)
	{
		SetupMovementCancel();
	}
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
		OnMovementCancelTriggered();
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
		OnMovementCancelTriggered();
		K2_EndAbility();
	}
}

bool UExtraGameplayAbility::HasMovementInput() const
{
	AExtraPlayerCharacter* AvatarChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (AvatarChar)
	{
		return AvatarChar->HasMoveInput();
	}

	return false;
}
