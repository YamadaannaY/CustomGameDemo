// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_Evade_Juhe.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"

UGA_Evade_Juhe::UGA_Evade_Juhe()
{
	// 居合 Montage 用到两类 MW 区间：
	//  - AttackFacing：架势 / 后撤段跟随锁定目标朝向；
	//  - ForwardOvershoot：前冲段穿过目标落到身后（穿透距离区间，Montage 里对应 NMS 填该名字）。
	bRotateToLockTarget = true;
	bEnableForwardOvershoot = true;
}

bool UGA_Evade_Juhe::ShouldEnterJuhe() const
{
	ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		return false;
	}

	// TODO 空中居合：空中满足条件也应走居合，空中表现/位移逻辑暂未实现，先回落基类空中 Evade
	if (AvatarChar->GetCharacterMovement()->IsFalling())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}

	// 二阶段普攻每次出手都会开定时窗口；窗口过期后只走普通 Evade，短按闪避进入居合
	if (!ASC->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetJuheReadyStateTag()))
	{
		return false;
	}

	return ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute()) >= JuheEnergyThreshold;
}

void UGA_Evade_Juhe::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// 条件不满足或未配置居合 Montage：完全交回基类 Evade,即普通闪避
	if (!ShouldEnterJuhe() || !JuheMontage)
	{
		Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
		return;
	}
	
	//同样消耗耐力
	if (!K2_CommitAbility())
	{
		K2_EndAbility();
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		K2_EndAbility();
		return;
	}
	
	bEnterJuheBranch = true;
	bJuhePhaseEnded = false;
	bJuheDodgeUsed = false;
	bPlayingForwardSegment = false;
	bJuheForwardStarted = false;
	JuheForwardIndex = 0;
	bJuheForwarding = false;
	bJuheForwardWindowOpen = false;

	// 居合进行中：挡住普攻 GA，直到分界事件、前冲链结束或Cancel窗口放行
	ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetJuheStateTag());

	// 消费居合窗口
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 0);

	if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		return;
	}

	// 普攻输入：架势段接前冲，前冲定时窗口内满足条件可以接下一段
	SetupWaitJuheAttackInput();

	// 居合 Montage 内的分界事件，监听到居合阶段结束，进入后摇的阶段
	UAbilityTask_WaitGameplayEvent* WaitPhaseEndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetJuhePhaseEndTag());
	WaitPhaseEndTask->EventReceived.AddDynamic(this, &ThisClass::OnJuhePhaseEnd);
	WaitPhaseEndTask->ReadyForActivation();

	// 居合期间唯一一次 Dodge：延迟一帧挂载，避免触发本次激活的输入被立即接收，用来退出居合
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::SetupWaitJuheDodgeInput);

	PlayJuheMontage(JuheMontage, false);
}

void UGA_Evade_Juhe::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 兜底：居合没走到分界/窗口结束就结束（被打断 / 动画播完）时也要放行普攻 GA
	RemoveJuheState();
	bEnterJuheBranch = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JuheForwardWindowTimer);
	}

	StopJuheMontage();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Evade_Juhe::PlayJuheMontage(UAnimMontage* Montage, bool bForwardSegment)
{
	if (!Montage)
	{
		return;
	}

	// 切段：先解绑旧任务回调再 EndTask，避免其收尾回调打断新段的流程
	StopJuheMontage();

	CurrentPlayingMontage = Montage;
	bPlayingForwardSegment = bForwardSegment;

	JuheMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage);
	JuheMontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->ReadyForActivation();
}

void UGA_Evade_Juhe::StopJuheMontage()
{
	if (!JuheMontageTask)
	{
		return;
	}

	JuheMontageTask->OnCompleted.RemoveDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnBlendOut.RemoveDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnInterrupted.RemoveDynamic(this, &ThisClass::OnJuheMontageFinished);
	JuheMontageTask->OnCancelled.RemoveDynamic(this, &ThisClass::OnJuheMontageFinished);

	if (JuheMontageTask->IsActive())
	{
		JuheMontageTask->EndTask();
	}

	JuheMontageTask = nullptr;
}

void UGA_Evade_Juhe::OnJuheMontageFinished()
{
	// 前冲段播完：还能接续就继续等下一次普攻（即寒意值还大于等于100）；窗口已关或能量不足则结束本 GA
	if (bPlayingForwardSegment)
	{
		bJuheForwarding = false;

		if (!CanChainJuheForward())
		{
			K2_EndAbility();
		}
		return;
	}

	// 居合架势段 / 后撤 Evade 段：播完照常结束
	K2_EndAbility();
}

void UGA_Evade_Juhe::SetupWaitJuheAttackInput()
{
	UAbilityTask_WaitGameplayEvent* WaitAttackTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetLightAttackInputTag(), nullptr, true, false);
	WaitAttackTask->EventReceived.AddDynamic(this, &ThisClass::OnJuheAttackInput);
	WaitAttackTask->ReadyForActivation();
}

void UGA_Evade_Juhe::SetupWaitJuheDodgeInput()
{
	UAbilityTask_WaitGameplayEvent* WaitDodgeTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetDodgeInputTag(), nullptr, true, false);
	WaitDodgeTask->EventReceived.AddDynamic(this, &ThisClass::OnJuheDodgeInput);
	WaitDodgeTask->ReadyForActivation();
}

void UGA_Evade_Juhe::OnJuheAttackInput(FGameplayEventData EventData)
{
	// 分界事件之后普攻归正常 Combo GA
	if (bJuhePhaseEnded)
	{
		return;
	}

	// 第一段前冲
	const bool bFirstForward = !bJuheForwardStarted;
	if (!bFirstForward && !CanChainJuheForward())
	{
		return;
	}

	if (!PickJuheForwardMontage())
	{
		return;
	}

	//重挂下一次输入监听形成循环，进行居合连段
	SetupWaitJuheAttackInput();

	StartJuheForward();
}

void UGA_Evade_Juhe::StartJuheForward()
{
	UAnimMontage* ForwardMontage = PickJuheForwardMontage();
	if (!ForwardMontage)
	{
		return;
	}

	// 扣除寒意值（PreAttributeBaseChange 已按 [0, EnergyMaxValue] 封顶）
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float CurrentEnergyValue = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
		ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetEnergyValueAttribute(), CurrentEnergyValue - JuheEnergyCost);
	}

	bJuheForwardStarted = true;
	bJuheForwarding = true;

	// 每段前冲都重置接续窗口
	bJuheForwardWindowOpen = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(JuheForwardWindowTimer, this, &ThisClass::CloseJuheForwardWindow, JuheForwardWindow, false);
	}

	PlayJuheMontage(ForwardMontage, true);

	// 段序推进，供下一次接续轮切
	++JuheForwardIndex;
}

UAnimMontage* UGA_Evade_Juhe::PickJuheForwardMontage() const
{
	// 偶数段（含首段）用前冲 1，奇数段用前冲 2
	UAnimMontage* Montage = (JuheForwardIndex % 2 == 0) ? JuheForwardMontage : JuheForwardMontage2;
	
	return Montage ? Montage : JuheForwardMontage;
}

bool UGA_Evade_Juhe::CanChainJuheForward() const
{
	// 接续窗口开着且剩余寒意值够，才能接下一段前冲
	return bJuheForwardWindowOpen && HasEnoughEnergyForJuheForward();
}

bool UGA_Evade_Juhe::HasEnoughEnergyForJuheForward() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}

	return ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute()) >= JuheEnergyThreshold;
}

void UGA_Evade_Juhe::CloseJuheForwardWindow()
{
	bJuheForwardWindowOpen = false;

	// 窗口过期时若没有在播前冲，本段居合到此为止（结束即放行普攻 GA）
	if (!bJuheForwarding)
	{
		K2_EndAbility();
	}
}

void UGA_Evade_Juhe::OnJuhePhaseEnd(FGameplayEventData EventData)
{
	// 前冲段的分界：只在「接不了下一段前冲」时才放行普攻 GA，因为实际设计中，前冲段很短，而窗口长于前冲段，需要提前判断分界要不要结束居合
	// 能量够就继续挡住，让普攻被本 GA 接管去接续前冲；否则放行给 Combo。
	if (bPlayingForwardSegment)
	{
		if (!CanChainJuheForward())
		{
			RemoveJuheState();
		}
		return;
	}

	// 居合架势段的分界事件：此后普攻交还正常 Combo
	bJuhePhaseEnded = true;
	RemoveJuheState();
}

void UGA_Evade_Juhe::OnJuheDodgeInput(FGameplayEventData EventData)
{
	if (bJuheDodgeUsed || !BackwardEvadeMontage)
	{
		return;
	}

	bJuheDodgeUsed = true;

	// 居合中再次闪避：走基类正常后撤 Evade 动画，播完结束
	RemoveJuheState();
	
	Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo())->GetWeaponComponent()->HideWeapon();

	PlayJuheMontage(BackwardEvadeMontage, false);
}

void UGA_Evade_Juhe::RemoveJuheState()
{
	if (!bEnterJuheBranch)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (ASC->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetJuheStateTag()))
		{
			ASC->RemoveLooseGameplayTag(UUExtraAbilitySystemStatic::GetJuheStateTag());
		}
	}
}