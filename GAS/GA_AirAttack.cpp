#include "GA_AirAttack.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_AirAttack::UGA_AirAttack()
{
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());
	
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirAttackTag());

	// 通过InputTag触发
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

	// 标记本次浮空已触发空中攻击，落地时清除
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetAirAttackTag());
	}

	// 锁定移动输入
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(AvatarChar))
	{
		PlayerChar->SetMovementInputLocked(true);
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
	AnimInst->Montage_Play(AirAttackStartMontage);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UGA_AirAttack::OnStartMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, AirAttackStartMontage);
}

void UGA_AirAttack::OnStartMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (bInterrupted)
	{
		K2_EndAbility();
		return;
	}

	PlayLoopMontage();
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
	AnimInst->Montage_PlayWithBlendIn(AirAttackLoopMontage, FAlphaBlend(StartToLoopBlendInTime), 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, true);

	// 强制 section 自循环：不依赖资产 bLoop 是否勾选，确保下砸循环动画播完会跳回自身，
	// 避免播完一遍后被 AnimBP 状态机接管切到 falling，导致 GA 与动画状态脱节。
	if (const FName LoopSection = AnimInst->Montage_GetCurrentSection(AirAttackLoopMontage); LoopSection != NAME_None)
	{
		AnimInst->Montage_SetNextSection(LoopSection, LoopSection, AirAttackLoopMontage);
	}

	// 绑定角色落地事件：落地即切换落地动画
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &UGA_AirAttack::OnLandDetected);
	}

	// 兜底：定时检查是否已落地（防止 LandedDelegate 边缘情况漏触发）
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			LandCheckTimerHandle,
			this,
			&UGA_AirAttack::PollLandCheck,
			0.05f,
			true);
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
	if (CurrentPhase != EAirAttackPhase::Loop)
	{
		return;
	}

	// 双重校验：确实已落地（不再是 falling）
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (AvatarChar->GetCharacterMovement() && AvatarChar->GetCharacterMovement()->IsFalling())
		{
			return; // 仍在空中，忽略本次（Timer 会继续轮询）
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

	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = EAirAttackPhase::Land;
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

	// 解锁移动输入
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(false);
	}

	// 停止可能仍在播放的动画
	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		if (AirAttackLoopMontage && AnimInst->Montage_IsPlaying(AirAttackLoopMontage))
		{
			AnimInst->Montage_Stop(0.2f, AirAttackLoopMontage);
		}
		if (AirAttackStartMontage && AnimInst->Montage_IsPlaying(AirAttackStartMontage))
		{
			AnimInst->Montage_Stop(0.2f, AirAttackStartMontage);
		}
		if (AirAttackLandMontage && AnimInst->Montage_IsPlaying(AirAttackLandMontage))
		{
			AnimInst->Montage_Stop(0.2f, AirAttackLandMontage);
		}
	}

	CurrentPhase = EAirAttackPhase::None;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
