#include "GA_KnockDown.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_KnockDown::UGA_KnockDown()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetKnockDownEventTag());
	SetAssetTags(AssetTags);

	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = UUExtraAbilitySystemStatic::GetKnockDownEventTag();
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);

	// 倒地期间挡住普通受击：由本 GA 独占，避免倒地动画被下一次受击顶掉
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetHitReactEventTag());

	// 打断被击者正在进行的主动能力（层级匹配，"ability.xxx" 全部覆盖，含正在播的受击）
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAbilityTag());
}

void UGA_KnockDown::OnReactActivated(const FGameplayEventData* TriggerEventData)
{
	if (!Cast<ACharacter>(GetAvatarActorFromActorInfo())
		|| !KnockDownStartMontage || !KnockDownLoopMontage || !KnockDownEndMontage)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	CurrentPhase = EKnockDownPhase::None;
	bHasLeftGround = false;
	
	PlayStartMontage();
}

UAnimInstance* UGA_KnockDown::GetAvatarAnimInstance() const
{
	if (const USkeletalMeshComponent* MeshComp = GetOwningComponentFromActorInfo())
	{
		return MeshComp->GetAnimInstance();
	}
	return nullptr;
}

void UGA_KnockDown::PlayStartMontage()
{
	CurrentPhase = EKnockDownPhase::Start;

	// 起手阶段就开始监听落地：低空被击飞时可能还没播完就落地了
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &UGA_KnockDown::OnLandDetected);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LandCheckTimerHandle, this, &UGA_KnockDown::PollLandCheck, LandCheckInterval, true);
	}

	UAbilityTask_PlayMontageAndWait* PlayStartTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, KnockDownStartMontage);
	PlayStartTask->OnBlendOut.AddDynamic(this,&ThisClass::OnStartMontageFinished);
	PlayStartTask->OnInterrupted.AddDynamic(this, &UGA_KnockDown::OnStartMontageInterrupted);
	PlayStartTask->OnCancelled.AddDynamic(this, &UGA_KnockDown::OnStartMontageInterrupted);
	PlayStartTask->ReadyForActivation();
}

void UGA_KnockDown::OnStartMontageFinished()
{
	if (CurrentPhase != EKnockDownPhase::Start)
	{
		return;
	}

	PlayLoopMontage();
}

void UGA_KnockDown::OnStartMontageInterrupted()
{
	if (CurrentPhase != EKnockDownPhase::Start)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_KnockDown::PlayLoopMontage()
{
	UAnimInstance* AnimInst = GetAvatarAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = EKnockDownPhase::Loop;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->PlayMontage(this, GetCurrentActivationInfo(), KnockDownLoopMontage, 1.0f);
	}

	/*
	// section 自循环
	if (const FName LoopSection = AnimInst->Montage_GetCurrentSection(KnockDownLoopMontage); LoopSection != NAME_None)
	{
		AnimInst->Montage_SetNextSection(LoopSection, LoopSection, KnockDownLoopMontage);
	}*/

	// 监听 Loop 被外部打断：主动停 Loop 进 End 时相位已是 End，回调会直接返回
	FOnMontageEnded LoopEndDelegate;
	LoopEndDelegate.BindUObject(this, &UGA_KnockDown::OnLoopMontageEnded);
	AnimInst->Montage_SetEndDelegate(LoopEndDelegate, KnockDownLoopMontage);
}

void UGA_KnockDown::OnLoopMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (CurrentPhase != EKnockDownPhase::Loop)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_KnockDown::OnLandDetected(const FHitResult& Hit)
{
	TryTriggerLand();
}

void UGA_KnockDown::PollLandCheck()
{
	TryTriggerLand();
}

void UGA_KnockDown::TryTriggerLand()
{
	// Start 阶段也可能落地（低空击飞），两个相位都允许
	if (CurrentPhase != EKnockDownPhase::Start && CurrentPhase != EKnockDownPhase::Loop)
	{
		return;
	}

	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		return;
	}
	const UCharacterMovementComponent* MoveComp = AvatarChar->GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	// 地面起手的击飞：先等角色真的离地。LaunchCharacter 只是设了速度，
	// MovementMode 要到下一帧 PerformMovement 才变成 Falling，不设这道门闩会当场误判落地
	if (!bHasLeftGround)
	{
		bHasLeftGround = MoveComp->IsFalling();
		return;
	}

	if (!MoveComp->IsFalling())
	{
		PlayEndMontage();
	}
}

void UGA_KnockDown::PlayEndMontage()
{
	// 清理落地检测
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_KnockDown::OnLandDetected);
	}

	// 先切相位再停动画：停 Start / Loop 触发的回调会因相位已变而直接返回
	CurrentPhase = EKnockDownPhase::End;

	StopLoopMontage();

	UAnimInstance* AnimInst = GetAvatarAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	// Start 阶段直接落地时 Start 可能还在播：此刻 ASC 记账的当前段已是 Loop，
	// StopMontageIfCurrent 够不到它，直连收掉（本就在淡出，模拟端那份会自己结束）
	if (KnockDownStartMontage && AnimInst->Montage_IsPlaying(KnockDownStartMontage))
	{
		AnimInst->Montage_StopWithBlendOut(KnockDownStartMontage->BlendOut, KnockDownStartMontage);
	}

	// 经 ASC 播放落地段：必须走这条通道模拟端才收得到
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->PlayMontage(this, GetCurrentActivationInfo(), KnockDownEndMontage, 1.0f);
	}

	// 播完（含起身）结束 GA
	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UGA_KnockDown::OnEndMontageEnded);
	AnimInst->Montage_SetEndDelegate(EndDelegate, KnockDownEndMontage);
}

void UGA_KnockDown::StopLoopMontage()
{
	if (!KnockDownLoopMontage)
	{
		return;
	}

	// 走 ASC 停：只有这条通道会写进复制数据。
	// StopMontageIfCurrent 自带「还是不是当前这段」判定
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->StopMontageIfCurrent(*KnockDownLoopMontage);
	}
}

void UGA_KnockDown::OnEndMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	K2_EndAbility();
}

void UGA_KnockDown::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 清理落地检测 Timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	// 解绑落地事件
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_KnockDown::OnLandDetected);
	}

	// 三段 Montage 收尾：ASC 记账中的当前段走 ASC 停（模拟端才同步停），
	// 三个 StopMontageIfCurrent 各自带判定，实际命中的只有一个；
	// 若当前段已被别的 GA 抢走，三个都不命中，不会误停别人的动画
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (KnockDownEndMontage) { ASC->StopMontageIfCurrent(*KnockDownEndMontage); }
		if (KnockDownLoopMontage) { ASC->StopMontageIfCurrent(*KnockDownLoopMontage); }
		if (KnockDownStartMontage) { ASC->StopMontageIfCurrent(*KnockDownStartMontage); }
	}

	CurrentPhase = EKnockDownPhase::None;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}