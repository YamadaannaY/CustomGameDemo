#include "GA_AirAttack.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/ExtraGameAnimInstance.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_AirAttack::UGA_AirAttack()
{
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());
	
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirAttackTag());

	// 启用移动打断
	bEnableMovementCancel = true;

	// 通过InputTag +AirBoneTag触发
	FAbilityTriggerData LightAttackTrigger;
	LightAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	LightAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
	AbilityTriggers.Add(LightAttackTrigger);
}

void UGA_AirAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
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

	if (!AirAttackStartMontage || !AirAttackLoopMontage || !AirAttackLandMontage)
	{
		K2_EndAbility();
		return;
	}

	// 标记触发空中攻击，落地时清除
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetAirAttackTag());
	}

	// 锁定Start + Loop过程中移动输入
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(AvatarChar))
	{
		PlayerChar->SetMovementInputLocked(true);
	}

	// 标记空中攻击中：让 AnimBP 状态机在攻击期间不让 falling 抢占动画输出
	if (UExtraGameAnimInstance* AnimInst = Cast<UExtraGameAnimInstance>(GetOwnerAnimInstance()))
	{
		AnimInst->bAirAttacking = true;
	}

	CurrentPhase = EAirAttackPhase::None;

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		PlayStartMontage();
	}
}

void UGA_AirAttack::PlayStartMontage()
{
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = EAirAttackPhase::Start;

	// 从 Start 阶段就监听落地：低空起跳可能 Start 还没播完就已落地，提前捕获以便直接进 Land。
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &UGA_AirAttack::OnLandDetected);
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			LandCheckTimerHandle,
			this,
			&UGA_AirAttack::PollLandCheck,
			0.08f,
			true);
	}

	// 用 PlayMontageAndWait，在 Start 的 BlendOut（开始淡出）时就触发 OnStartMontageBlendOut，
	// 让 Loop 与 Start 的淡出重叠，避免「完全结束后才播 Loop」造成的真空帧导致进入状态机。
	UAbilityTask_PlayMontageAndWait* PlayStartTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, AirAttackStartMontage, 1.0f, NAME_None, false, 1.0f);
	PlayStartTask->OnBlendOut.AddDynamic(this, &UGA_AirAttack::OnStartMontageBlendOut);
	PlayStartTask->OnInterrupted.AddDynamic(this, &UGA_AirAttack::OnStartMontageInterrupted);
	PlayStartTask->OnCancelled.AddDynamic(this, &UGA_AirAttack::OnStartMontageInterrupted);
	PlayStartTask->ReadyForActivation();
}

void UGA_AirAttack::OnStartMontageBlendOut()
{
	if (CurrentPhase != EAirAttackPhase::Start)
	{
		return;
	}

	PlayLoopMontage();
}

void UGA_AirAttack::OnStartMontageInterrupted()
{
	// 仅 Start 阶段的中断才是外部打断(即起跳montage期间直接落地的少数情况)；Loop/Land 阶段主动停止 Start 是正常流程。
	if (CurrentPhase != EAirAttackPhase::Start)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_AirAttack::PlayLoopMontage()
{
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = EAirAttackPhase::Loop;
	AnimInst->Montage_PlayWithBlendIn(AirAttackLoopMontage, FAlphaBlend(StartToLoopBlendInTime), 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);

	//section 自循环：不依赖资产 bLoop 是否勾选，确保下砸循环动画播完会跳回自身，
	if (const FName LoopSection = AnimInst->Montage_GetCurrentSection(AirAttackLoopMontage); LoopSection != NAME_None)
	{
		AnimInst->Montage_SetNextSection(LoopSection, LoopSection, AirAttackLoopMontage);
	}
}

void UGA_AirAttack::OnLandDetected(const FHitResult& Hit)
{
	TryTriggerLand();
}

void UGA_AirAttack::PollLandCheck()
{
	TryTriggerLand();
}

void UGA_AirAttack::TryTriggerLand()
{
	// Start 阶段也可能落地（低空起跳），因此允许 Start 与 Loop 两阶段触发
	if (CurrentPhase != EAirAttackPhase::Start && CurrentPhase != EAirAttackPhase::Loop)
	{
		return;
	}

	// 双重校验：确实已落地（不再是 falling）
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (AvatarChar->GetCharacterMovement() && AvatarChar->GetCharacterMovement()->IsFalling())
		{
			return;
		}
	}

	PlayLandMontage();
}

void UGA_AirAttack::PlayLandMontage()
{
	// 清理落地检测
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_AirAttack::OnLandDetected);
	}

	StopLoopMontage();

	// 落地阶段解锁移动输入：角色已在地面，允许移动并让移动打断机制生效
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(false);
	}

	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	// 先切换阶段，再停 Start：停止会触发 OnStartMontageInterrupted，
	// 此时已是 Land 阶段，回调会直接 return，避免误结束 GA。
	CurrentPhase = EAirAttackPhase::Land;

	// Start 阶段就落地时，Start montage 可能仍在播放，直接停掉
	if (AirAttackStartMontage && AnimInst->Montage_IsPlaying(AirAttackStartMontage))
	{
		AnimInst->Montage_Stop(LoopToLandBlendInTime, AirAttackStartMontage);
	}

	AnimInst->Montage_PlayWithBlendIn(AirAttackLandMontage, FAlphaBlend(LoopToLandBlendInTime), 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UGA_AirAttack::OnLandMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, AirAttackLandMontage);
}

void UGA_AirAttack::StopLoopMontage()
{
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (AnimInst && AirAttackLoopMontage && AnimInst->Montage_IsPlaying(AirAttackLoopMontage))
	{
		AnimInst->Montage_Stop(LoopToLandBlendInTime, AirAttackLoopMontage);
	}
}

void UGA_AirAttack::OnMovementCancelTriggered()
{
	// 立即让 AnimBP 状态机接管：若等 EndAbility 才清 bAirAttacking，
	// 状态机过渡会晚于 montage 停止一拍，导致移动响应额外延迟。
	if (UExtraGameAnimInstance* AnimInst = Cast<UExtraGameAnimInstance>(GetOwnerAnimInstance()))
	{
		AnimInst->bAirAttacking = false;
	}
}

void UGA_AirAttack::OnLandMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	K2_EndAbility();
}

void UGA_AirAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 清理落地检测 Timer
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	// 解绑落地事件
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_AirAttack::OnLandDetected);
	}

	// 清除本次浮空的空中攻击标记（落地后允许下次浮空再触发）
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(UUExtraAbilitySystemStatic::GetAirAttackTag());
	}

	// 清除 AnimBP 的空中攻击标记，让状态机恢复正常
	if (UExtraGameAnimInstance* AnimInst = Cast<UExtraGameAnimInstance>(GetOwnerAnimInstance()))
	{
		AnimInst->bAirAttacking = false;
	}

	// 解锁移动输入
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(false);
	}

	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		if (AirAttackLoopMontage && AnimInst->Montage_IsPlaying(AirAttackLoopMontage))
		{
			AnimInst->Montage_Stop(MontageCancelBlendOutTime, AirAttackLoopMontage);
		}
		if (AirAttackStartMontage && AnimInst->Montage_IsPlaying(AirAttackStartMontage))
		{
			AnimInst->Montage_Stop(MontageCancelBlendOutTime, AirAttackStartMontage);
		}
		if (AirAttackLandMontage && AnimInst->Montage_IsPlaying(AirAttackLandMontage))
		{
			AnimInst->Montage_Stop(MontageCancelBlendOutTime, AirAttackLandMontage);
		}
	}

	CurrentPhase = EAirAttackPhase::None;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
