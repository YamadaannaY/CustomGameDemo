#include "GA_AirAttack_Phase2.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ExtractGameCharacter/Projectile/ExtraSwordQi.h"
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

	FAbilityTriggerData LightAttackTrigger;
	LightAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	LightAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
	AbilityTriggers.Add(LightAttackTrigger);
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

	// 段数只活在本次激活里：每次激活都从动画1开始（不缓存）
	StageIndex = 0;
	bComboWindowOpen = false;
	bTransitioning = false;
	bInLanding = false;
	bHandingOff = false;
	CurrentPlayingMontage = nullptr;

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		// 轻击输入：全程监听，是否生效由窗口与段数决定（窗口外直接丢弃）
		UAbilityTask_WaitGameplayEvent* InputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, UUExtraAbilitySystemStatic::GetLightAttackInputTag(), nullptr, false, true);
		InputTask->EventReceived.AddDynamic(this, &ThisClass::OnLightAttackInput);
		InputTask->ReadyForActivation();

		// 可衔接窗口开 / 关（Montage 上的 AN_AttackComboWindow 发送）
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

		PlayStage(0);
	}

	// 落地即结束：委托监听为主，轮询兜底（激活瞬间已贴地等边界情况）
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
		World->GetTimerManager().SetTimer(JuheReadyTimer, this, &ThisClass::ClearJuheReady, JuheReadyWindow, false);
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
	// 正在主动切段 / 进落地段时，不结束GA
	if (bTransitioning)
	{
		return;
	}

	// 本段播完仍没接段 → 本次连斩结束
	K2_EndAbility();
}

void UGA_AirAttack_Phase2::OnStageMontageInterrupted()
{
	// 主动切段触发的停止不算作打断：
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

void UGA_AirAttack_Phase2::OnLightAttackInput(FGameplayEventData Payload)
{
	if (bTransitioning)
	{
		return;
	}

	// 不缓存：窗口之外的输入一律丢弃，不会攒到本段结束时补出下一段
	if (!bComboWindowOpen)
	{
		return;
	}

	if (StageIndex >= HandoffStageIndex)
	{
		// 第 2 段的窗口内再按一次 → 第三段交给 GA_AirAttack 的下砸，本 GA 结束
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
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	// 发空中下砸专属 Tag 触发 GA_AirAttack。
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		Avatar, UUExtraAbilitySystemStatic::GetAirDiveInputTag(), FGameplayEventData());
}

void UGA_AirAttack_Phase2::SetupSwordSlashListener()
{
	// OnlyMatchExact=true：只接住 AN 发的这一个精确 tag，不误接 ability.airattack.* 下的其他事件
	UAbilityTask_WaitGameplayEvent* WaitSwordQiTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetAirAttackSwordQiTag(), nullptr, false, true);
	WaitSwordQiTask->EventReceived.AddDynamic(this, &ThisClass::HandleSwordQiRequest);
	WaitSwordQiTask->ReadyForActivation();
}

void UGA_AirAttack_Phase2::HandleSwordQiRequest(FGameplayEventData EventData)
{
	SpawnSwordQi();
}

void UGA_AirAttack_Phase2::SpawnSwordQi()
{
	// 权威端生成：AN 的事件两端都会触发，不加判断联机下会双端各生成一道
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

	SwordQi->InitProjectile(Char, SwordQiDamageEffect, static_cast<int32>(GetAbilityLevel()), FireDir, SwordQiSpeed, SwordQiLifeTime);
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
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (AnimInst && CurrentPlayingMontage && AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
	{
		AnimInst->Montage_StopWithBlendOut(CurrentPlayingMontage->BlendOut, CurrentPlayingMontage);
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

	// 兜底停掉仍在播的段（落地段结束 / 被打断路径）；先置 bTransitioning 防止回调重复结束GA
	bTransitioning = true;
	StopCurrentPlayingMontage();

	CurrentPlayingMontage = nullptr;
	bComboWindowOpen = false;
	bInLanding = false;
	StageIndex = 0;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}