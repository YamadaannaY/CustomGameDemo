#include "GA_Evade.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UGA_Evade::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	//消耗耐力
	if (!K2_CommitAbility())
	{
		K2_EndAbility();
		return;
	}

	ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		K2_EndAbility();
		return;
	}

	//基于速度判断前/后 Evade
	const FVector Velocity = AvatarChar->GetVelocity();
	float Speed = Velocity.Size2D();
	
	CurrentPlayingMontage = (Speed > 0.f) ? ForwardEvadeMontage : BackwardEvadeMontage;
	if (!CurrentPlayingMontage)
	{
		K2_EndAbility();
		return;
	}

	//初始化
	bTransitionedToSprint = false;
	bIsPollingForInput = false;
	CurrentEvadeFacingOffset = 0.f;

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
		if (!AnimInst)
		{
			K2_EndAbility();
			return;
		}

		AnimInst->Montage_Play(CurrentPlayingMontage);

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &UGA_Evade::OnEvadeMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentPlayingMontage);

		UAbilityTask_WaitGameplayEvent* WaitToSprintTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, FGameplayTag::RequestGameplayTag("Evade.ToSprint"));
		WaitToSprintTask->EventReceived.AddDynamic(this, &ThisClass::OnEvadeToSprint);
		WaitToSprintTask->ReadyForActivation();

		AExtraPlayerCharacter* PC = Cast<AExtraPlayerCharacter>(AvatarChar);
		
		//前冲Evade允许基于MWC进行朝向调整，由于RootMotion，需要将InputDirYaw当做前方基准Yaw而非ActorRotYaw
		if (PC && CurrentPlayingMontage == ForwardEvadeMontage)
		{
			const FVector& InitInput = PC->GetInputDirection();
			if (InitInput.IsNearlyZero())
			{
				EvadeBaseYaw = AvatarChar->GetActorRotation().Yaw;
			}
			else
			{
				EvadeBaseYaw = FRotationMatrix::MakeFromX(InitInput).Rotator().Yaw;
			}
			
			//定时器每帧应用MR修改朝向
			GetWorld()->GetTimerManager().SetTimer(EvadeFacingTimer, this, &UGA_Evade::UpdateEvadeFacing, EvadeFacingUpdateInterval, true);
		}
	}
}

void UGA_Evade::UpdateEvadeFacing()
{
	const AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerChar)
	{
		GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);
		return;
	}

	UAnimInstance* AnimInst = PlayerChar->GetMesh()->GetAnimInstance();
	if (!AnimInst || !CurrentPlayingMontage || !AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
	{
		GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);
		return;
	}

	const float TargetOffset = [&]() -> float
	{
		if (!PlayerChar->HasMoveInput())
		{
			return 0.f;
		}

		const FVector& InputDir = PlayerChar->GetInputDirection();
		if (InputDir.IsNearlyZero())
		{
			return 0.f;
		}

		// 以 EvadeBaseYaw 为"前"，计算输入方向在左右轴上的投影
		const FVector EvadeForward = FRotationMatrix(FRotator(0.f, EvadeBaseYaw, 0.f)).GetUnitAxis(EAxis::X);
		const FVector EvadeRight = FRotationMatrix(FRotator(0.f, EvadeBaseYaw, 0.f)).GetUnitAxis(EAxis::Y);

		const float RawRight = FVector::DotProduct(InputDir, EvadeRight);
		float TargetYaw = EvadeBaseYaw + FMath::Clamp(RawRight, -1.f, 1.f) * EvadeMaxRotationAngle;

		return FMath::FindDeltaAngleDegrees(EvadeBaseYaw, TargetYaw);
	}();

	CurrentEvadeFacingOffset = FMath::FInterpTo(CurrentEvadeFacingOffset, TargetOffset, EvadeFacingUpdateInterval, EvadeRotationInterpSpeed);

	const FRotator TargetRot(0.f, EvadeBaseYaw + CurrentEvadeFacingOffset, 0.f);

	if (UMotionWarpingComponent* MWC = PlayerChar->GetMotionWarpingComponent())
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("EvadeFacing");
		WarpTarget.Location = PlayerChar->GetActorLocation();
		WarpTarget.Rotation = TargetRot;
		MWC->AddOrUpdateWarpTarget(WarpTarget);
	}
}

void UGA_Evade::OnEvadeToSprint(FGameplayEventData EventData)
{
	GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);

	if (bTransitionedToSprint || bIsPollingForInput)
	{
		return;
	}

	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerChar)
	{
		return;
	}
	
	//此AN标记动画帧之后，移动输入可以打断Montage直接结束GA，并且将Sprint标志位置为true
	if (PlayerChar->HasMoveInput())
	{
		PollMoveInputForSprint();
		return;
	}

	bIsPollingForInput = true;
	GetWorld()->GetTimerManager().SetTimer(InputPollTimer, this, &UGA_Evade::PollMoveInputForSprint, InputPollInterval, true);
}

void UGA_Evade::PollMoveInputForSprint()
{
	if (!CurrentPlayingMontage)
	{
		GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
		return;
	}

	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerChar)
	{
		GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
		return;
	}

	UAnimInstance* AnimInst = PlayerChar->GetMesh()->GetAnimInstance();
	if (!AnimInst || !AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
	{
		GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
		return;
	}

	if (!PlayerChar->HasMoveInput())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
	bTransitionedToSprint = true;

	AnimInst->Montage_Stop(SprintTransitionBlendOut, CurrentPlayingMontage);
}

void UGA_Evade::OnEvadeMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	K2_EndAbility();
}

void UGA_Evade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
		GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);
	}

	if (CurrentPlayingMontage)
	{
		ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		if (AvatarChar)
		{
			UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
			{
				AnimInst->Montage_Stop(0.2f, CurrentPlayingMontage);
			}
		}
		CurrentPlayingMontage = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
