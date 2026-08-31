#include "GA_Evade.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_Evade::UGA_Evade()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetDodgeAbilityTag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetDodgeAbilityTag());

	// 不可打断Tag存在期间（SkillGA 表现段）不可激活；后摇段放开后，激活时取消 SkillGA 打断其后摇。
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	// 通过 InputTag 触发
	FAbilityTriggerData DodgeTrigger;
	DodgeTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	DodgeTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetDodgeInputTag();
	AbilityTriggers.Add(DodgeTrigger);
}

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

	// 以角色朝向为"前"判定前/后 Evade：
	const bool bAirborne = AvatarChar->GetCharacterMovement()->IsFalling();
	const AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(AvatarChar);

	bool bBackwardInput = true;
	if (PlayerChar && PlayerChar->HasMoveInput())
	{
		const FVector& InputDir = PlayerChar->GetInputDirection();
		if (!InputDir.IsNearlyZero())
		{
			bBackwardInput = FVector::DotProduct(AvatarChar->GetActorForwardVector(), InputDir) < 0.f;
		}
	}

	const bool bForwardInput = !bBackwardInput;

	bPlayingForwardEvade = bForwardInput;
	CurrentPlayingMontage = bAirborne
		? (bForwardInput ? ForwardAirEvadeMontage : BackwardAirEvadeMontage)
		: (bForwardInput ? ForwardEvadeMontage : BackwardEvadeMontage);
	if (!CurrentPlayingMontage)
	{
		K2_EndAbility();
		return;
	}

	//init
	bTransitionedToSprint = false;
	bIsPollingForInput = false;
	bEvadeToSprintTriggered = false;
	bAirborne == true ? DodgeCount = 2 : DodgeCount = 1;
	CurrentEvadeFacingOffset = 0.f;

	// 空中 Evade：绑定落地委托，落地立即结束 GA。地面 Evade 不绑。
	bAirborneEvade = bAirborne;
	if (bAirborneEvade)
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &ThisClass::OnAirEvadeLandDetected);
	}

	//播放Montage
	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		PlayEvadeMontage();

		UAbilityTask_WaitGameplayEvent* WaitToSprintTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, UUExtraAbilitySystemStatic::GetEvadeToSprintTag());
		WaitToSprintTask->EventReceived.AddDynamic(this, &ThisClass::OnEvadeToSprint);
		WaitToSprintTask->ReadyForActivation();

		// 监听第二次 Dodge 输入：第0帧~EvadeToSprint 期间可再闪避一次。
		// 必须延迟到下一帧再挂载监听，否则输入直接触发此InputTask
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::SetupWaitDodgeInputPress);

		AExtraPlayerCharacter* PC = Cast<AExtraPlayerCharacter>(AvatarChar);

		//前冲Evade允许基于MWC进行朝向调整，由于RootMotion，需要将InputDirYaw当做前方基准Yaw而非ActorRotYaw
		if (PC && bPlayingForwardEvade)
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

void UGA_Evade::PlayEvadeMontage()
{
	UAbilityTask_PlayMontageAndWait* EvadeMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, CurrentPlayingMontage);
	HandlePlayMontageTaskDelegates(EvadeMontageTask);
	EvadeMontageTask->ReadyForActivation();
}

void UGA_Evade::HandlePlayMontageTaskDelegates(UAbilityTask_PlayMontageAndWait* Task)
{
	if (!Task)
	{
		return;
	}
	
	if (PlayEvadeMontageTask && PlayEvadeMontageTask->IsActive())
	{
		PlayEvadeMontageTask->EndTask();
	}
	PlayEvadeMontageTask = Task;

	Task->OnCompleted.AddDynamic(this, &ThisClass::K2_EndAbility);
	Task->OnBlendOut.AddDynamic(this, &ThisClass::K2_EndAbility);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::K2_EndAbility);
	Task->OnCancelled.AddDynamic(this, &ThisClass::K2_EndAbility);
}

void UGA_Evade::SetupWaitDodgeInputPress()
{
	UAbilityTask_WaitGameplayEvent* WaitDodgeInputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetDodgeInputTag(), nullptr, true, false);
	WaitDodgeInputTask->EventReceived.AddDynamic(this, &ThisClass::HandleDodgeInputPress);
	WaitDodgeInputTask->ReadyForActivation();
}

void UGA_Evade::HandleDodgeInputPress(FGameplayEventData EventData)
{
	// 与 GA_Combo 一致：先重新挂载监听，形成循环接收后续输入，再由次数守卫决定是否响应
	SetupWaitDodgeInputPress();

	// 仅在第0帧~EvadeToSprint 通知期间响应，且次数未用尽
	if (bEvadeToSprintTriggered || DodgeCount >= MaxDodgeCount)
	{
		return;
	}

	if (!CurrentPlayingMontage)
	{
		return;
	}

	ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		return;
	}

	UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
	if (!AnimInst || !AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
	{
		return;
	}

	DodgeCount++;
	PlayEvadeMontage();
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

	// EvadeToSprint 通知之后不再响应再次闪避
	bEvadeToSprintTriggered = true;

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

void UGA_Evade::OnAirEvadeLandDetected(const FHitResult& Hit)
{
	// 落地立即结束 GA；montage 停止由 EndAbility 统一用 MontageCancelBlendOutTime 处理
	K2_EndAbility();
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

	AnimInst->Montage_Stop(MontageCancelBlendOutTime, CurrentPlayingMontage);
}

void UGA_Evade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(InputPollTimer);
		GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);
	}

	// 空中 Evade：解绑落地委托，避免悬空绑定
	if (bAirborneEvade)
	{
		if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			AvatarChar->LandedDelegate.RemoveDynamic(this, &ThisClass::OnAirEvadeLandDetected);
		}
		bAirborneEvade = false;
	}

	if (CurrentPlayingMontage)
	{
		ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		if (AvatarChar)
		{
			UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
			{
				AnimInst->Montage_Stop(MontageCancelBlendOutTime, CurrentPlayingMontage);
			}
		}
		CurrentPlayingMontage = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}