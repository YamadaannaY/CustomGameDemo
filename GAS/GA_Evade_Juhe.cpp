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
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}

	// 地面 / 空中同一套条件：二阶段普攻每次出手都会开定时窗口；窗口过期后只走普通 Evade
	if (!ASC->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetJuheReadyStateTag()))
	{
		return false;
	}

	return ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute()) >= JuheEnergyThreshold;
}

void UGA_Evade_Juhe::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// 先确定本次是地面还是空中居合（决定用哪套动画）
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	bAirJuhe = AvatarChar && AvatarChar->GetCharacterMovement()->IsFalling();

	// 条件不满足或未配置居合 Montage：完全交回基类 Evade,即普通闪避
	if (!ShouldEnterJuhe() || !GetActiveJuheMontages().JuheMontage)
	{
		bAirJuhe = false;
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
	bInLanding = false;
	bJuhePhaseEnded = false;
	bJuheDodgeUsed = false;
	bPlayingForwardSegment = false;
	bJuheForwardStarted = false;
	JuheForwardIndex = 0;
	bJuheForwarding = false;
	bJuheForwardWindowOpen = false;

	// 居合进行中：挡住普攻 GA，直到分界事件、前冲链结束或Cancel窗口放行
	ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetJuheStateTag());

	// 消费普攻开启的居合窗口（地面 / 空中一致）
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 0);

	if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		return;
	}

	// 空中居合：落地即转入落地段（委托为主，轮询兜底）
	if (bAirJuhe)
	{
		if (ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			Avatar->LandedDelegate.AddDynamic(this, &ThisClass::OnJuheLanded);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(JuheLandCheckTimer, this, &ThisClass::PollJuheLandCheck, LandCheckInterval, true);
		}
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

	PlayJuheMontage(GetActiveJuheMontages().JuheMontage, false);
}

void UGA_Evade_Juhe::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 兜底：居合没走到分界/窗口结束就结束（被打断 / 动画播完）时也要放行普攻 GA
	RemoveJuheState();
	ClearJuheLandDetection();
	bEnterJuheBranch = false;
	bAirJuhe = false;
	bInLanding = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JuheForwardWindowTimer);
	}

	StopJuheMontage();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Evade_Juhe::PlayJuheMontage(UAnimMontage* Montage, bool bForwardSegment, bool bDodgeSegment)
{
	if (!Montage)
	{
		return;
	}

	// 切段：先解绑旧任务回调再 EndTask，避免其收尾回调打断新段的流程
	StopJuheMontage();

	CurrentPlayingMontage = Montage;
	bPlayingForwardSegment = bForwardSegment;
	bPlayingDodgeSegment = bDodgeSegment;

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
	// 居合中被 Dodge 打断的后撤 Evade 段：与地面 Evade 一致，播完直接结束 GA
	// （不按空中居合那样等落地、也不接落地段）
	if (bPlayingDodgeSegment)
	{
		K2_EndAbility();
		return;
	}

	// 前冲段播完：还能接续就继续等下一次普攻（即寒意值还大于等于100）；窗口已关或能量不足则收尾
	if (bPlayingForwardSegment)
	{
		bJuheForwarding = false;

		if (CanChainJuheForward())
		{
			return;
		}

		// 接不了下一段：空中居合还要等落地播完落地动画再结束
		if (TryHoldForAirLanding())
		{
			return;
		}

		K2_EndAbility();
		return;
	}

	// 居合架势段 / 后撤 Evade 段 / 落地段：同样先看空中是否还要等落地
	if (TryHoldForAirLanding())
	{
		return;
	}

	K2_EndAbility();
}

bool UGA_Evade_Juhe::TryHoldForAirLanding()
{
	if (!bAirJuhe || bInLanding)
	{
		return false;
	}

	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (AvatarChar && AvatarChar->GetCharacterMovement()->IsFalling())
	{
		// 还在空中：不结束 GA，继续等普攻（接前冲）或等落地
		return true;
	}

	// 已贴地但落地检测还没轮到：直接进落地段（落地动画播完会再回到这里，此时 bInLanding 已为 true）
	EnterLandPhase();
	return true;
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

	// 二阶段攻击强化进度：计的是「居合」打出的前冲段（架势段本身不计）——
	// 每触发一次前冲段算一次，含连段接续的后续段。打满 AttackProJuheRequired 次挂上就绪标记，
	if (UAbilitySystemComponent* ForwardASC = GetAbilitySystemComponentFromActorInfo())
	{
		const int32 ForwardCount = ForwardASC->GetTagCount(UUExtraAbilitySystemStatic::GetProJuheCountTag());
		
		//居合次数超过三次不会再进入此if内，即ProReadyTag不再被这段代码修改，永远为1等待爆发重击GA进行消耗。
		if (ForwardCount < UUExtraAbilitySystemStatic::AttackProJuheRequired)
		{
			const int32 NextCount = ForwardCount + 1;
			ForwardASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetProJuheCountTag(), NextCount);

			if (NextCount >= UUExtraAbilitySystemStatic::AttackProJuheRequired)
			{
				ForwardASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetProReadyTag(), 1);
			}
		}
	}
}

const FJuheMontageSet& UGA_Evade_Juhe::GetActiveJuheMontages() const
{
	return bAirJuhe ? AirMontages : GroundMontages;
}

UAnimMontage* UGA_Evade_Juhe::PickJuheForwardMontage() const
{
	const FJuheMontageSet& Set = GetActiveJuheMontages();

	// 偶数段（含首段）用前冲 1，奇数段用前冲 2
	UAnimMontage* Montage = (JuheForwardIndex % 2 == 0) ? Set.ForwardMontage1 : Set.ForwardMontage2;

	// 未配置前冲 2 时回落前冲 1
	return Montage ? Montage : Set.ForwardMontage1;
}

void UGA_Evade_Juhe::OnJuheLanded(const FHitResult& Hit)
{
	EnterLandPhase();
}

void UGA_Evade_Juhe::PollJuheLandCheck()
{
	// 委托在「激活瞬间已经贴地」这类边界情况下不会触发，靠轮询兜底
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		return;
	}

	const UCharacterMovementComponent* MoveComp = AvatarChar->GetCharacterMovement();
	if (MoveComp && !MoveComp->IsFalling())
	{
		EnterLandPhase();
	}
}

void UGA_Evade_Juhe::EnterLandPhase()
{
	// 落地委托与轮询可能同时触达，只处理第一次
	if (bInLanding)
	{
		return;
	}
	bInLanding = true;

	ClearJuheLandDetection();

	// 落地即放弃剩余的前冲接续窗口
	bJuheForwardWindowOpen = false;
	bJuheForwarding = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JuheForwardWindowTimer);
	}

	// 放行普攻 GA：落地动画按普通后摇处理
	RemoveJuheState();

	if (!AirLandMontage)
	{
		K2_EndAbility();
		return;
	}

	// 播完自动结束 GA（走 OnJuheMontageFinished 的非前冲段分支）
	PlayJuheMontage(AirLandMontage, false);
}

void UGA_Evade_Juhe::ClearJuheLandDetection()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JuheLandCheckTimer);
	}

	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &ThisClass::OnJuheLanded);
	}
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

	if (bJuheForwarding)
	{
		return;
	}

	// 窗口过期、且没有在播前冲：本段居合到此为止。
	// 但空中居合还要等落地播完落地动画再结束，否则落地时已经没有 GA 了。
	if (TryHoldForAirLanding())
	{
		return;
	}

	K2_EndAbility();
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

	// 空中居合走空中后撤动画，地面走地面后撤动画
	UAnimMontage* DodgeMontage = bAirJuhe ? BackwardAirEvadeMontage : BackwardEvadeMontage;
	if (bJuheDodgeUsed || !DodgeMontage)
	{
		return;
	}

	bJuheDodgeUsed = true;

	// 居合中再次闪避：走基类正常后撤 Evade 动画，播完结束
	RemoveJuheState();

	Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo())->GetWeaponComponent()->HideWeapon();

	PlayJuheMontage(DodgeMontage, false, true);

	// 重置重力必须放在切段之后：PlayJuheMontage 会停掉前冲段，而挂在它上面的 ANS_GravityScale
	// 在那一刻仍会按曲线把重力写回滞空值（NotifyEnd 未勾 RestoreOnEnd 时不会恢复），
	// 放在切段之前就会被这次写入覆盖，后撤段便一直保持前冲段的滞空重力
	Cast<ACharacter>(GetAvatarActorFromActorInfo())->GetCharacterMovement()->GravityScale = DefaultGravityScale;
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