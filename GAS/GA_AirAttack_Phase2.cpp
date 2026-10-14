#include "GA_AirAttack_Phase2.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputPress.h"
#include "Animation/AnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ExtractGameCharacter/Projectile/ExtraSwordQi.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

UGA_AirAttack_Phase2::UGA_AirAttack_Phase2()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetAirAttackAbilityTag());
	SetAssetTags(AssetTags);

	// 与其他空中攻击互斥，并阻止自身重入激活
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAirAttackAbilityTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirAttackAbilityTag());

	// 居合进行中不可激活：空中居合接管空中输入（普攻 / 闪避），避免同一次普攻把空中攻击也带起来
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetJuheStateTag());

	// 仅空中（跳跃 / 下落）可触发
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());
	// 仅二阶段形态
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetPhase2StateTag());

	// 启用通用武器碰撞伤害（基类机制）：Montage 上的轨迹 AN 命中即按 DefaultDamageEffect 结算
	bEnableWeaponDamage = true;

	// 启用基类重力机制：连段各段的 ANS_GravityScale 覆写
	bEnableGravityScale = true;

	// 落地段后摇可打断：CancelWindow 机制（Montage 后摇段放 AN_CancelWindow）
	bEnableMovementCancel = true;
	bEnableCancelWindow = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

	InputTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
}

void UGA_AirAttack_Phase2::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!K2_CommitAbility())
	{
		K2_EndAbility();
		return;
	}

	if (!AttackMontage1 || !AttackMontage2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_AirAttack_Phase2] 两段 Montage 未配置完整，取消激活"));
		K2_EndAbility();
		return;
	}

	// 先清掉本轮激活的内存状态 
	StageIndex = 0;
	bComboWindowOpen = false;
	bTransitioning = false;
	bInLanding = false;
	bHandingOff = false;
	CurrentPlayingMontage = nullptr;

	// 段数存在 ASC 的 tag count 上，落地时清零。
	// 0 = 尚未打出，1 = 段1 已打出，2 = 段1+段2 都已打出，3 = 三段打满。
	int32 PlayedStageCount = 0;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		PlayedStageCount = ASC->GetTagCount(UUExtraAbilitySystemStatic::GetAirAttackStageTag());
	}

	// 本次腾空已打满三段：不再响应
	if (PlayedStageCount >= MaxAirAttackStages)
	{
		K2_EndAbility();
		return ;
	}

	// 前两段都已打出：这次按下的就是第三段，结束自身GA并且触发AirAttackGA
	if (PlayedStageCount >= MaxAirAttackStages - 1)
	{
		HandoffToDiveAttack();
		return;
	}

	// 从本次腾空尚未打出的那一段接着打
	StageIndex = PlayedStageCount;

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		SetupLightAttackInputListener();

		// 可衔接窗口开 / 关
		
		UAbilityTask_WaitGameplayEvent* WindowBeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, UUExtraAbilitySystemStatic::GetAirAttackComboBeginTag(), nullptr, false, true);
		WindowBeginTask->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowBegin);
		WindowBeginTask->ReadyForActivation();

		UAbilityTask_WaitGameplayEvent* WindowEndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, UUExtraAbilitySystemStatic::GetAirAttackComboEndTag(), nullptr, false, true);
		WindowEndTask->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowEnd);
		WindowEndTask->ReadyForActivation();

		// 剑气：Montage 挥刀帧的 AN 触发一次，每收到一次生成一道剑气
		SetupSwordSlashListener();

		// 从本次腾空尚未打出的那一段接着打
		PlayStage(PlayedStageCount);
	}

	// 落地即结束：委托监听为主，轮询兜底
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &ThisClass::OnLanded);
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(LandCheckTimerHandle, this, &ThisClass::PollLandCheck, LandCheckInterval, true);
	}
}

UAnimMontage* UGA_AirAttack_Phase2::GetMontageForStage(int32 InIndex) const
{
	return (InIndex % 2 == 0) ? AttackMontage1 : AttackMontage2;
}

void UGA_AirAttack_Phase2::PlayStage(int32 InIndex)
{
	if (!GetOwnerAnimInstance())
	{
		K2_EndAbility();
		return;
	}

	CurrentPlayingMontage = GetMontageForStage(InIndex);
	if (!CurrentPlayingMontage)
	{
		K2_EndAbility();
		return;
	}
	
	// 记进度：本次腾空已打出到第 InIndex+1 段，下次激活直接从下一段开始播放
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetLooseGameplayTagCount
		(UUExtraAbilitySystemStatic::GetAirAttackStageTag(), InIndex + 1);
	}

	//重置窗口
	bComboWindowOpen = false;
	bTransitioning = false;

	// 本段出手即开启居合窗口（与地面普攻一致：每段出手都开，空中居合据此判定可触发）
	OpenJuheReadyWindow();

	UAbilityTask_PlayMontageAndWait* StageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, CurrentPlayingMontage);
	StageTask->OnCompleted.AddDynamic(this, &ThisClass::OnStageMontageCompleted);
	StageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnStageMontageInterrupted);
	StageTask->OnCancelled.AddDynamic(this, &ThisClass::OnStageMontageInterrupted);
	StageTask->ReadyForActivation();
}

void UGA_AirAttack_Phase2::OpenJuheReadyWindow()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	JuheReadyASC = ASC;

	// 用 SetLooseGameplayTagCount 置 1 而非 AddLooseGameplayTag：后者是计数累加语义，
	// 连续进段会把计数堆到 N，到期只减 1 会残留 tag，导致窗口永不关闭。
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 1);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(JuheReadyTimer, this, &ThisClass::ClearJuheReady, JuheReadyWindowSeconds, false);
	}
}

void UGA_AirAttack_Phase2::ClearJuheReady()
{
	if (UAbilitySystemComponent* ASC = JuheReadyASC.Get())
	{
		ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 0);
	}
}

void UGA_AirAttack_Phase2::OnStageMontageCompleted()
{
	if (bTransitioning)
	{
		return;
	}

	// 本段播完仍没接段 → 本次连斩结束
	K2_EndAbility();
}

void UGA_AirAttack_Phase2::OnStageMontageInterrupted()
{
	if (bTransitioning)
	{
		return;
	}

	// 外部打断（如空中闪避抢占同 slot）
	K2_EndAbility();
}

void UGA_AirAttack_Phase2::OnComboWindowBegin(FGameplayEventData Payload)
{
	bComboWindowOpen = true;
}

void UGA_AirAttack_Phase2::OnComboWindowEnd(FGameplayEventData Payload)
{
	bComboWindowOpen = false;
}

void UGA_AirAttack_Phase2::SetupLightAttackInputListener()
{
	// WaitInputPress 是一次性的：每次收到输入后由回调重新挂载，形成全程监听
	UAbilityTask_WaitInputPress* InputTask = UAbilityTask_WaitInputPress::WaitInputPress(this);
	InputTask->OnPress.AddDynamic(this, &ThisClass::OnLightAttackInput);
	InputTask->ReadyForActivation();
}

void UGA_AirAttack_Phase2::OnLightAttackInput(float TimeWaited)
{
	SetupLightAttackInputListener();

	if (bTransitioning)
	{
		return;
	}
	
	//必须在窗口内
	if (!bComboWindowOpen)
	{
		return;
	}

	if (StageIndex >= HandoffStageIndex)
	{
		//第三段，此时触发AirAttack_GA
		HandoffToDiveAttack();
		return;
	}

	AdvanceToNextStage();
}

void UGA_AirAttack_Phase2::HandoffToDiveAttack()
{
	if (bHandingOff)
	{
		return;
	}
	bHandingOff = true;
	
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetAirAttackStageTag(), MaxAirAttackStages);
	}

	// 先结束自身：BlockAbilitiesWithTag 对 airattack 的封锁随结束一起释放，
	// 常驻的 GA_AirAttack 这时才可能被激活
	K2_EndAbility();

	// 结束栈内直接激活另一个 GA 是重入，错开一帧再触发
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::TriggerDiveHandoff);
	}
}

void UGA_AirAttack_Phase2::TriggerDiveHandoff()
{
	// 按下空中下砸的输入 Tag：GA_AirAttack 按它绑定，两端各自的 ASC 都会处理到这次输入
	if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->AbilityInputTagPressed(UUExtraAbilitySystemStatic::GetAirDiveInputTag());
		ASC->ProcessAbilityInput();
	}
}

void UGA_AirAttack_Phase2::SetupSwordSlashListener()
{
	UAbilityTask_WaitGameplayEvent* WaitSwordQiTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetAirAttackSwordQiTag(), nullptr, false, true);
	WaitSwordQiTask->EventReceived.AddDynamic(this, &ThisClass::HandleSwordQiRequest);
	WaitSwordQiTask->ReadyForActivation();
}

void UGA_AirAttack_Phase2::HandleSwordQiRequest(FGameplayEventData EventData)
{
	SpawnSwordQi(EventData.EventMagnitude);
}

void UGA_AirAttack_Phase2::SpawnSwordQi(float RollOffset)
{
	// 权威端生成,依赖复制同步到客户端
	if (!K2_HasAuthority() || !SwordQiActorClass)
	{
		return;
	}

	AExtraPlayerCharacter* Char = GetOwningAvatarCharacter();
	if (!Char)
	{
		return;
	}

	UExtraGameWeaponComponent* WeaponComp = Char->GetWeaponComponent();
	if (!WeaponComp)
	{
		return;
	}

	UStaticMeshComponent* SwordMesh = WeaponComp->GetWeaponMeshByTag(SwordWeaponTag);

	const FVector SpawnLoc = SwordMesh->GetSocketLocation(SwordQiSpawnSocketName);

	// 方向：有锁定目标就朝目标飞，否则沿角色正前方
	FVector FireDir = Char->GetActorForwardVector();
	if (const AActor* LockTarget = Char->GetLockTarget())
	{
		const FVector ToTarget = LockTarget->GetActorLocation() - SpawnLoc;
		if (ToTarget.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			FireDir = ToTarget.GetSafeNormal();
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Char;
	SpawnParams.Instigator = Char;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AExtraSwordQi* SwordQi = World->SpawnActor<AExtraSwordQi>(SwordQiActorClass, SpawnLoc, FireDir.Rotation(), SpawnParams);
	if (!SwordQi)
	{
		return;
	}

	SwordQi->InitProjectile(Char, SwordQiDamageEffect, (GetAbilityLevel()), FireDir, SwordQiSpeed, SwordQiLifeTime, RollOffset);
}

void UGA_AirAttack_Phase2::AdvanceToNextStage()
{
	++StageIndex;

	// 先置位再停：Montage_Stop 会同步触发上一段任务的 Interrupted 回调
	bTransitioning = true;
	StopCurrentPlayingMontage();

	PlayStage(StageIndex);
}

void UGA_AirAttack_Phase2::StopCurrentPlayingMontage()
{
	if (!CurrentPlayingMontage)
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->StopMontageIfCurrent(*CurrentPlayingMontage);
	}
}

void UGA_AirAttack_Phase2::EnterLandPhase()
{
	// 落地委托与轮询 Timer 可能同时触达，只处理第一次
	if (bInLanding)
	{
		return;
	}
	bInLanding = true;

	ClearLandDetection();

	// 先置 bTransitioning 再停：Stop 会同步触发上一段的 Interrupted 回调，属正常流程
	bTransitioning = true;
	bComboWindowOpen = false;
	
	StopCurrentPlayingMontage();

	if (!LandMontage)
	{
		K2_EndAbility();
		return;
	}

	CurrentPlayingMontage = LandMontage;
	
	UAbilityTask_PlayMontageAndWait* LandTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, LandMontage);
	LandTask->OnCompleted.AddDynamic(this, &ThisClass::OnLandMontageFinished);
	LandTask->OnInterrupted.AddDynamic(this, &ThisClass::OnLandMontageFinished);
	LandTask->OnCancelled.AddDynamic(this, &ThisClass::OnLandMontageFinished);
	LandTask->ReadyForActivation();
}

void UGA_AirAttack_Phase2::ClearLandDetection()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &ThisClass::OnLanded);
	}
}

void UGA_AirAttack_Phase2::OnLandMontageFinished()
{
	K2_EndAbility();
}

void UGA_AirAttack_Phase2::OnLanded(const FHitResult& Hit)
{
	EnterLandPhase();
}

void UGA_AirAttack_Phase2::PollLandCheck()
{
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		return;
	}

	if (const UCharacterMovementComponent* MoveComp = AvatarChar->GetCharacterMovement())
	{
		if (!MoveComp->IsFalling())
		{
			EnterLandPhase();
		}
	}
}

void UGA_AirAttack_Phase2::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearLandDetection();

	// 兜底停掉仍在播的段；先置 bTransitioning 防止回调重复结束GA
	bTransitioning = true;
	StopCurrentPlayingMontage();

	CurrentPlayingMontage = nullptr;
	bComboWindowOpen = false;
	bInLanding = false;
	StageIndex = 0;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}