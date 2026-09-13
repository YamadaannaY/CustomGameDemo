// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_Evade_Juhe.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

UGA_Evade_Juhe::UGA_Evade_Juhe()
{
	// 启用 AttackFacing warp target：居合 Montage 的 MW 区间由基类每帧写入落点与朝向
	bRotateToLockTarget = true;
}

FVector UGA_Evade_Juhe::ComputeLockOnWarpLocation(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget, bool& bOutWarpTranslation) const
{
	if (!PlayerChar)
	{
		bOutWarpTranslation = false;
		return FVector::ZeroVector;
	}

	// 非前冲段（居合架势 / 后撤 Evade）：只跟随朝向，不做位移 warp，避免被拉向目标
	if (!bPlayingForwardSegment || !LockTarget)
	{
		bOutWarpTranslation = false;
		return PlayerChar->GetActorLocation();
	}

	bOutWarpTranslation = true;

	// 前冲：沿本段锁定的冲刺方向穿过目标，落在目标身后 JuheForwardOvershoot 处。
	// 不用基类的 MotionWarpMaxMoveDist（那个值针对「落到目标身前」的常规攻击，会把落点钳在目标前），
	// 改用独立上限 JuheForwardMaxWarpDist 做保护，避免目标过远时瞬移过大。
	const FVector ForwardDir = JuheForwardFaceDir.IsNearlyZero() ? DirToTarget : JuheForwardFaceDir;
	const FVector OvershootLocation = LockTarget->GetActorLocation() + ForwardDir * JuheForwardOvershoot;

	const float DistanceToOvershoot = FVector::Dist2D(OvershootLocation, PlayerChar->GetActorLocation());
	if (DistanceToOvershoot > JuheForwardMaxWarpDist)
	{
		return PlayerChar->GetActorLocation() + ForwardDir * JuheForwardMaxWarpDist;
	}

	return OvershootLocation;
}

FVector UGA_Evade_Juhe::ComputeLockOnFaceDir(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget) const
{
	// 前冲段：朝向锁定为本段起手方向，避免穿身后朝目标方向反转
	if (bPlayingForwardSegment && !JuheForwardFaceDir.IsNearlyZero())
	{
		return JuheForwardFaceDir;
	}

	return Super::ComputeLockOnFaceDir(PlayerChar, LockTarget, DirToTarget);
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
	// 条件不满足或未配置居合 Montage：完全交回基类 Evade
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
	JuheForwardFaceDir = FVector::ZeroVector;

	// 居合进行中：挡住普攻 GA，直到分界事件、前冲链结束或Cancel窗口放行
	ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetJuheStateTag());

	// 消费居合窗口
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 0);

	if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		return;
	}

	// 普攻输入：架势段接前冲，前冲窗口内接下一段
	SetupWaitJuheAttackInput();

	// 居合 Montage 内的分界事件
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
	RemoveJuheForwardCollisionIgnore();
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

		// 位移已结束，恢复与目标的碰撞
		RemoveJuheForwardCollisionIgnore();

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

	// 第一段前冲：居合架势段按普攻即可；后续段需满足窗口/能量条件
	const bool bFirstForward = !bJuheForwardStarted;
	if (!bFirstForward && !CanChainJuheForward())
	{
		return;
	}

	if (!PickJuheForwardMontage())
	{
		return;
	}

	// 与 GA_Combo 一致：重挂下一次输入监听形成循环
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

	AddJuheForwardCollisionIgnore();

	// 锁定本段冲刺方向：穿过目标后「角色→目标」会反向，逐帧跟随会让 warp 落点在
	// 穿越瞬间从目标一侧翻到另一侧，角色位置跳变、镜头抖动。故每段起手时定一次。
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (const AActor* LockTarget = PlayerChar->GetLockTarget())
		{
			JuheForwardFaceDir = LockTarget->GetActorLocation() - PlayerChar->GetActorLocation();
			JuheForwardFaceDir.Z = 0.f;
			JuheForwardFaceDir.Normalize();
		}
	}

	// 扣能量（PreAttributeBaseChange 已按 [0, EnergyMaxValue] 封顶）
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float CurrentEnergyValue = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
		ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetEnergyValueAttribute(), CurrentEnergyValue - JuheEnergyCost);
	}

	bJuheForwardStarted = true;
	bJuheForwarding = true;

	// 每段前冲都重置接续窗口；前冲播放期间按普攻同样算接续输入
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

void UGA_Evade_Juhe::AddJuheForwardCollisionIgnore()
{
	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	UWorld* World = GetWorld();
	if (!PlayerChar || !World)
	{
		return;
	}

	// 先清掉上一次的忽略，避免残留
	RemoveJuheForwardCollisionIgnore();

	// 前冲穿身：忽略场上所有 ExtraCharacter（含未锁定的）。
	// 不忽略的话，MW 位移仍走胶囊 swept 检测，会被它们的胶囊挡住。
	for (TActorIterator<AExtraCharacter> It(World); It; ++It)
	{
		AExtraCharacter* Other = *It;
		if (!Other || Other == PlayerChar)
		{
			continue;
		}

		PlayerChar->MoveIgnoreActorAdd(Other);
		JuheIgnoredActors.Add(Other);
	}
}

void UGA_Evade_Juhe::RemoveJuheForwardCollisionIgnore()
{
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		for (const TWeakObjectPtr<AActor>& Ignored : JuheIgnoredActors)
		{
			if (AActor* IgnoredActor = Ignored.Get())
			{
				PlayerChar->MoveIgnoreActorRemove(IgnoredActor);
			}
		}
	}

	JuheIgnoredActors.Reset();
}

bool UGA_Evade_Juhe::CanChainJuheForward() const
{
	// 接续窗口开着且剩余能量够，才能再接一段前冲
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
	// 前冲动画 CancelWindow 开头的分界：只在「接不了下一段前冲」时才放行普攻 GA。
	// 能量够就继续挡住，让普攻被本 GA 接管去接续前冲；否则放行给 Combo。
	// 注意 HandleGameplayEvent 里 AbilityTriggers 激活早于 WaitGameplayEvent 广播，
	// 所以这里只能决定「是否维持阻挡」，无法在回调里临时放行本次已派发的输入。
	if (bPlayingForwardSegment)
	{
		if (!CanChainJuheForward())
		{
			RemoveJuheState();
		}
		return;
	}

	// 居合本段的分界事件：此后普攻交还正常 Combo
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