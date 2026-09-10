#include "GA_Evade.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/ExtraGameAnimInstance.h"
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

 // 不可打断Tag存在期间（SkillGA 表现段）不可激活；后摇段放开后，激活时SkillGA 打断其后摇。
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	// 同理：重击 GA 表现段挂霸体时本 GA 无法激活，其 M_End 后摇放开霸体后，激活即打断重击接管
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag());

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

	const bool bAirborne = AvatarChar->GetCharacterMovement()->IsFalling();
	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(AvatarChar);

	//init
	bTransitionedToSprint = false;
	bIsPollingForInput = false;
	bEvadeToSprintTriggered = false;
	bAirborne == true ? DodgeCount = 2 : DodgeCount = 1;
	CurrentEvadeFacingOffset = 0.f;

	// 每次闪避动作（首次激活 / 二次闪避）都基于当前输入重新选择 montage：
	// 无输入 → 原地后闪；任意方向输入 → 前冲并 MW 快速扭转到输入方向。
	// 置于空中消耗预算之前，保证 montage 缺失时提前结束、不吞空中预算。
	if (!ReselectEvade(bAirborne))
	{
		K2_EndAbility();
		return;
	}

	// 空中 Evade：消耗一次空中闪避预算（CanActivateAbility 校验）
	if (bAirborne && PlayerChar)
	{
		PlayerChar->ConsumeAirEvade();
	}

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
		// 空中 Evade 一次只闪一次：空中闪避次数由本次浮空的预算管理（见 AExtraPlayerCharacter），
		// 想再次闪避应通过空中攻击恢复预算后重新激活 GA，故空中不挂此监听。
		// 延迟到下一帧再挂载监听，否则输入直接触发此InputTask导致一次函数调用
		if (!bAirborne)
		{
			GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::SetupWaitDodgeInputPress);
		}

		// 播放期间每帧按当前输入在基准 ±EvadeMaxRotationAngle 内插值微调朝向（初始 target 已在 Montage 开播前写入）
		if (bPlayingForwardEvade)
		{
			GetWorld()->GetTimerManager().SetTimer(EvadeFacingTimer, this, &UGA_Evade::UpdateEvadeFacing, GetWorld()->GetDeltaSeconds(), true);
		}
	}
}

bool UGA_Evade::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 空中闪避预算拦截：仅在空中且预算耗尽时拒绝激活；地面 Evade 不受限制。
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (ACharacter* AvatarChar = Cast<ACharacter>(ActorInfo->AvatarActor.Get()))
		{
			if (AvatarChar->GetCharacterMovement()->IsFalling())
			{
				if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(AvatarChar))
				{
					return PlayerChar->CanUseAirEvade();
				}
			}
		}
	}
	return true;
}

bool UGA_Evade::ReselectEvade(bool bAirborne)
{
	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());

	// 前冲闪避需要有效移动输入；无输入（或输入方向尚未就绪）→ 原地后闪
	const bool bForward = PlayerChar && PlayerChar->HasMoveInput() && !PlayerChar->GetInputDirection().IsNearlyZero();

	UAnimMontage* NewMontage = bAirborne
		? (bForward ? ForwardAirEvadeMontage : BackwardAirEvadeMontage)
		: (bForward ? ForwardEvadeMontage : BackwardEvadeMontage);
	if (!NewMontage)
	{
		return false;
	}

	bPlayingForwardEvade = bForward;
	CurrentPlayingMontage = NewMontage;
	CurrentEvadeFacingOffset = 0.f;

	if (bForward)
	{
		// 朝向基准取当前输入方向，MW 将角色从当前朝向快速扭转至此方向
		EvadeBaseYaw = FRotationMatrix::MakeFromX(PlayerChar->GetInputDirection()).Rotator().Yaw;
		ApplyEvadeFacingWarp(PlayerChar);
	}

	return true;
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
	// 与 GA_Combo 逻辑一致：先重新监听下一次输入，形成循环，再由次数守卫决定是否响应
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

	// 二次闪避与首次一致：基于此刻的移动输入重新选择 montage 与转向方向，
	// 而不是复用本次激活触发时锁定的 montage（原地后闪与输入转向各自独立，均不继承上一次朝向基准）。
	if (!ReselectEvade(bAirborneEvade))
	{
		// 防御：当前无可用 montage，回滚计数并保持正在播放的闪避动画继续
		DodgeCount--;
		return;
	}

	// 让朝向微调 timer 与本次选择保持一致：
	// 前冲 → 刷新 timer 持续按当前输入微调朝向；原地后闪 → 停掉，避免残留 MW target 干扰后撤闪避
	if (bPlayingForwardEvade)
	{
		GetWorld()->GetTimerManager().SetTimer(EvadeFacingTimer, this, &UGA_Evade::UpdateEvadeFacing, GetWorld()->GetDeltaSeconds(), true);
	}
	else
	{
		GetWorld()->GetTimerManager().ClearTimer(EvadeFacingTimer);
	}

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
		
		const float TargetYaw = EvadeBaseYaw + FMath::Clamp(RawRight, -1.f, 1.f) * EvadeMaxRotationAngle;

		return FMath::FindDeltaAngleDegrees(EvadeBaseYaw, TargetYaw);
	}();

	CurrentEvadeFacingOffset = FMath::FInterpTo(CurrentEvadeFacingOffset, TargetOffset, GetWorld()->GetDeltaSeconds(), EvadeRotationInterpSpeed);

	ApplyEvadeFacingWarp(PlayerChar);
}

void UGA_Evade::ApplyEvadeFacingWarp(const AExtraPlayerCharacter* PlayerChar)
{
	if (!PlayerChar)
	{
		return;
	}

	if (UMotionWarpingComponent* MWC = PlayerChar->GetMotionWarpingComponent())
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("EvadeFacing");
		WarpTarget.Location = PlayerChar->GetActorLocation();
		WarpTarget.Rotation = FRotator(0.f, EvadeBaseYaw + CurrentEvadeFacingOffset, 0.f);
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

	// 玩家按住移动进入冲刺：置冲刺模式（物理提速）+ 放行 ABP Sprint 过渡 + 用目标速度喂一帧避免截断后塌速
	PlayerChar->SetSprinting(true);
	if (UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(PlayerChar->GetMesh()->GetAnimInstance()))
	{
		GameAI->bEvadeToSprint = true;
	}
	const FVector SprintDir = PlayerChar->GetInputDirection();
	if (!SprintDir.IsNearlyZero())
	{
		PlayerChar->SprintTransitionVelocity = SprintDir * PlayerChar->SprintSpeed;
	}

	AnimInst->Montage_Stop(MontageCancelBlendOutTime, CurrentPlayingMontage);
}

void UGA_Evade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	//地面使用了连续Evade机制时应用一个冷却GE
	if (MaxDodgeTriggerCooldownEffect && DodgeCount>=MaxDodgeCount && !bAirborneEvade)
	{
		if (!K2_HasAuthority())
		{
			return;
		}
		
		UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
		if (!SourceASC)
		{
			return;
		}

		AActor* Avatar = GetAvatarActorFromActorInfo();
		if (!Avatar)
		{
			return;
		}

		const TSubclassOf<UGameplayEffect> CooldownGE = MaxDodgeTriggerCooldownEffect;
		if (!CooldownGE)
		{
			return;
		}

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddInstigator(Avatar, Avatar);
		Context.AddSourceObject(Avatar);
		FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(CooldownGE, GetAbilityLevel(), Context);
		
		SourceASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}
		
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