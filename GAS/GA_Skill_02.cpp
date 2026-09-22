#include "GA_Skill_02.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Character.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Skill_02::UGA_Skill_02()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetSkill02Tag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill02Tag());
	
	// 启用通用武器碰撞伤害（基类机制）
	bEnableWeaponDamage = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

	// 落地斩属于空中下落类攻击：落点必须拉出目标胶囊，否则会先被水平拖到目标正上方、
	LockOnWarpStandoff = 100.f;

	FAbilityTriggerData SkillTrigger;
	SkillTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	SkillTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetSkillInputTag();
	AbilityTriggers.Add(SkillTrigger);
}

bool UGA_Skill_02::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	// 原生实现（UGameplayAbility::CheckCooldown）只看 cooldown GE 的 GrantedTags 是否在 ASC 上，不认层数,
	// 那样会把「还剩 1 层充能」也一并封死。这里改成按层数放行
	const FGameplayTagContainer* CooldownTags = GetCooldownTags();
	if (!CooldownTags || CooldownTags->IsEmpty())
	{
		return true;
	}

	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return true;
	}

	const FGameplayEffectQuery CooldownQuery = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);

	int32 UsedCharges = 0;
	for (const FActiveGameplayEffectHandle& CooldownHandle : ASC->GetActiveEffects(CooldownQuery))
	{
		if (const FActiveGameplayEffect* ActiveCooldown = ASC->GetActiveGameplayEffect(CooldownHandle))
		{
			UsedCharges = FMath::Max(UsedCharges, ActiveCooldown->Spec.GetStackCount());
		}
	}

	if (UsedCharges < 2)
	{
		return true;
	}

	// 层数用满：沿用原生的失败约定，添加Tag并判定为false
	if (OptionalRelevantTags)
	{
		OptionalRelevantTags->AppendMatchingTags(ASC->GetOwnedGameplayTags(), *CooldownTags);
	}
	return false;
}

void UGA_Skill_02::DoDamage(const FGameplayEventData& Data)
{
	Super::DoDamage(Data);

	if (!K2_HasAuthority())
	{
		return;
	}
	
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	
	const float CurrentEnergyValue = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
	ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetEnergyValueAttribute(), CurrentEnergyValue + EnergyValuePerHit);
}

void UGA_Skill_02::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo, nullptr))
	{
		K2_EndAbility();
		return;
	}

	//init 
	
	CurrentPhase = ESkill02Phase::None;
	bRiseReadyNotify = false;
	bPendingSkillInput = false;

	if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		return;
	}

	//根据空地状态选择起手段
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const bool bAirborne = AvatarChar && AvatarChar->GetCharacterMovement() && AvatarChar->GetCharacterMovement()->IsFalling();

	if (bAirborne)
	{
		EnterLandAttack();
	}
	else
	{
		PlayRiseMontage();
	}
}

void UGA_Skill_02::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_Skill_02::OnLandDetected);
	}

	// 解锁移动输入
	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(false);
	}

	// 兜底停掉仍在播的段（异常结束 / 被打断路径），各段用自己 Montage 上配的 BlendOut
	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		if (RiseMontage && AnimInst->Montage_IsPlaying(RiseMontage))
		{
			AnimInst->Montage_StopWithBlendOut(RiseMontage->BlendOut, RiseMontage);
		}
		if (LandAttackStartMontage && AnimInst->Montage_IsPlaying(LandAttackStartMontage))
		{
			AnimInst->Montage_StopWithBlendOut(LandAttackStartMontage->BlendOut, LandAttackStartMontage);
		}
		if (LandAttackLoopMontage && AnimInst->Montage_IsPlaying(LandAttackLoopMontage))
		{
			AnimInst->Montage_StopWithBlendOut(LandAttackLoopMontage->BlendOut, LandAttackLoopMontage);
		}
		if (LandAttackLandMontage && AnimInst->Montage_IsPlaying(LandAttackLandMontage))
		{
			AnimInst->Montage_StopWithBlendOut(LandAttackLandMontage->BlendOut, LandAttackLandMontage);
		}
	}

	CurrentPhase = ESkill02Phase::None;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Skill_02::PlayRiseMontage()
{
	if (!RiseMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Skill_02] RiseMontage 未配置，取消激活"));
		K2_EndAbility();
		return;
	}

	CurrentPhase = ESkill02Phase::Rise;

	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(true);
	}

	UAbilityTask_PlayMontageAndWait* RiseTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, RiseMontage);
	RiseTask->OnBlendOut.AddDynamic(this, &ThisClass::OnRiseMontageFinished);
	RiseTask->OnCompleted.AddDynamic(this, &ThisClass::OnRiseMontageFinished);
	RiseTask->OnInterrupted.AddDynamic(this, &ThisClass::OnRiseMontageInterrupted);
	RiseTask->OnCancelled.AddDynamic(this, &ThisClass::OnRiseMontageInterrupted);
	RiseTask->ReadyForActivation();

	// 段1 期间才监听第二次技能输入：OnlyTriggerOnce 让这一段只接受一次切换，
	// 段2 开始后任务已结束，天然满足「落地斩期间不响应」。
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::SetupWaitSkillInput);
	}
	
	//监听可以接落地斩的AN事件，只监听一次
	UAbilityTask_WaitGameplayEvent* WaitRiseReadyTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetSkill02RiseReadyTag(), nullptr, true, true);
	WaitRiseReadyTask->EventReceived.AddDynamic(this, &ThisClass::OnRiseNotifyMarked);
	WaitRiseReadyTask->ReadyForActivation();

	// 长按进居合的检测帧监听（与「第二次输入」是独立通道，互不影响）
	SetupWaitJuheCheck();
}

void UGA_Skill_02::SetupWaitJuheCheck()
{
	// 检测帧事件由动画帧发出
	UAbilityTask_WaitGameplayEvent* WaitJuheCheckTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetSkill02JuheCheckTag(), nullptr, true, true);
	WaitJuheCheckTask->EventReceived.AddDynamic(this, &ThisClass::OnJuheCheckFrame);
	WaitJuheCheckTask->ReadyForActivation();
}

void UGA_Skill_02::OnJuheCheckFrame(FGameplayEventData Payload)
{
	// 段1 与落地段挂的是同一个 tag 的两个监听，可能同时活着；
	// 交接完成后本 GA 已结束，这里做幂等保护，保证只交接一次
	if (!IsActive())
	{
		return;
	}

	// 松手了就是普通技能：不进居合
	const AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerChar || !PlayerChar->IsHoldingSkill())
	{
		return;
	}

	// 能量不足等价于短按：同样不进居合
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	const float Energy = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
	if (Energy < JuheEnergyThreshold)
	{
		return;
	}

	// 走 GA_Evade_Juhe 原本的进居合路径：它要求 State.JuheReady + 能量够，
	// 这里补挂 JuheReady 放行（居合激活后会自己把它清零消费掉）
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 1);

	// 先发闪避输入把居合激活起来、再结束自己——避免在「结束自己」的调用栈里激活别人。
	if (AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			Avatar, UUExtraAbilitySystemStatic::GetDodgeInputTag(), FGameplayEventData());
	}

	K2_EndAbility();
}

void UGA_Skill_02::OnRiseNotifyMarked(FGameplayEventData Payload)
{
	//已标记
	bRiseReadyNotify = true;
	
	//落地斩
	TryEnterLandAttack();
}

void UGA_Skill_02::SetupWaitSkillInput()
{
	//只有升空段才能接再次输入回调
	if (!IsActive() || CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}
	
	UAbilityTask_WaitGameplayEvent* WaitSkillInputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetSkillInputTag(), nullptr, false, true);
	WaitSkillInputTask->EventReceived.AddDynamic(this, &ThisClass::OnSkillInputDuringRise);
	WaitSkillInputTask->ReadyForActivation();
}

void UGA_Skill_02::OnRiseMontageFinished()
{
	//说明是因为切段2导致的Finish，不响应
	if (CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}
	
	K2_EndAbility();
}

void UGA_Skill_02::OnRiseMontageInterrupted()
{
	//说明是因为切段2导致的Interrupt，不响应
	if (CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_Skill_02::OnSkillInputDuringRise(FGameplayEventData Payload)
{
	if (CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}

	//缓存
	bPendingSkillInput = true;
	
	TryEnterLandAttack();
}

void UGA_Skill_02::TryEnterLandAttack()
{
	//是否有输入
	if (CurrentPhase != ESkill02Phase::Rise || !bRiseReadyNotify || !bPendingSkillInput)
	{
		return;
	}

	// 必须仍在空中
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar || !AvatarChar->GetCharacterMovement() || !AvatarChar->GetCharacterMovement()->IsFalling())
	{
		return;
	}

	// 消耗第二层充能：K2_CommitAbilityCooldown
	if (!K2_CommitAbilityCooldown(false,false))
	{
		return;
	}

	//init
	bPendingSkillInput = false;
	
	EnterLandAttack();
}


void UGA_Skill_02::EnterLandAttack()
{
	// 先切阶段再停段1：Montage_Stop 会同步触发段1 的 Interrupted 回调，
	// 那时 CurrentPhase 已不是 Rise，回调会直接 return。
	CurrentPhase = ESkill02Phase::LandStart;

	//停Montage
	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		if (RiseMontage && AnimInst->Montage_IsPlaying(RiseMontage))
		{
			AnimInst->Montage_StopWithBlendOut(RiseMontage->BlendOut, RiseMontage);
		}
	}

	//Land
	PlayLandAttackStartMontage();
}

void UGA_Skill_02::PlayLandAttackStartMontage()
{
	if (!LandAttackStartMontage || !LandAttackLoopMontage || !LandAttackLandMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Skill_02] 落地斩三段 Montage 未配置完整，取消激活"));
		K2_EndAbility();
		return;
	}

	if (AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerChar->SetMovementInputLocked(true);
	}

	// 从起手段就监听落地：低空触发段2时可能在起手还没播完时就已经落地
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &UGA_Skill_02::OnLandDetected);
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(LandCheckTimerHandle, this, &UGA_Skill_02::PollLandCheck, 0.08f, true);
	}
	
	UAbilityTask_PlayMontageAndWait* StartTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, LandAttackStartMontage, 1.0f, NAME_None, false, 1.0f);
	StartTask->OnBlendOut.AddDynamic(this, &ThisClass::OnLandAttackStartBlendOut);
	StartTask->OnInterrupted.AddDynamic(this, &ThisClass::OnLandAttackStartInterrupted);
	StartTask->OnCancelled.AddDynamic(this, &ThisClass::OnLandAttackStartInterrupted);
	StartTask->ReadyForActivation();
}

void UGA_Skill_02::OnLandAttackStartBlendOut()
{
	if (CurrentPhase != ESkill02Phase::LandStart)
	{
		return;
	}

	PlayLandAttackLoopMontage();
}

void UGA_Skill_02::OnLandAttackStartInterrupted()
{
	//同上
	if (CurrentPhase != ESkill02Phase::LandStart)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_Skill_02::PlayLandAttackLoopMontage()
{
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (!AnimInst)
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = ESkill02Phase::LandLoop;
	
	//Montage LoopSection
	AnimInst->Montage_Play(LandAttackLoopMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);
	if (const FName LoopSection = AnimInst->Montage_GetCurrentSection(LandAttackLoopMontage); LoopSection != NAME_None)
	{
		AnimInst->Montage_SetNextSection(LoopSection, LoopSection, LandAttackLoopMontage);
	}

	// 监听循环被外部打断（如空中 Evade 抢占同 slot）：打断即结束 GA。
	// 主动停循环进落地时 CurrentPhase 已是 LandLand，回调直接 return，不误伤正常流程。
	FOnMontageEnded LoopEndDelegate;
	LoopEndDelegate.BindUObject(this, &UGA_Skill_02::OnLandAttackLoopEnded);
	AnimInst->Montage_SetEndDelegate(LoopEndDelegate, LandAttackLoopMontage);
}

void UGA_Skill_02::OnLandAttackLoopEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (CurrentPhase != ESkill02Phase::LandLoop)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_Skill_02::OnLandDetected(const FHitResult& Hit)
{
	TryTriggerLand();
}

void UGA_Skill_02::PollLandCheck()
{
	TryTriggerLand();
}

void UGA_Skill_02::TryTriggerLand()
{
	//检测
	if (CurrentPhase != ESkill02Phase::LandStart && CurrentPhase != ESkill02Phase::LandLoop)
	{
		return;
	}

	// 再次校验
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (AvatarChar->GetCharacterMovement() && AvatarChar->GetCharacterMovement()->IsFalling())
		{
			return;
		}
	}

	PlayLandAttackLandMontage();
}

void UGA_Skill_02::PlayLandAttackLandMontage()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LandCheckTimerHandle);
	}

	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.RemoveDynamic(this, &UGA_Skill_02::OnLandDetected);
	}

	// 先切阶段再停动画：停起手/循环会触发它们的回调，那时已是 LandLand，回调直接 return
	CurrentPhase = ESkill02Phase::LandLand;

	StopLandAttackLoopMontage();

	// 角色已在地面，解锁移动输入并让移动打断机制生效
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

	// 起手段可能仍在播（低空触发），直接停掉
	if (LandAttackStartMontage && AnimInst->Montage_IsPlaying(LandAttackStartMontage))
	{
		AnimInst->Montage_StopWithBlendOut(LandAttackStartMontage->BlendOut, LandAttackStartMontage);
	}

	AnimInst->Montage_Play(LandAttackLandMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);

	FOnMontageEnded LandEndDelegate;
	LandEndDelegate.BindUObject(this, &UGA_Skill_02::OnLandAttackLandMontageEnded);
	AnimInst->Montage_SetEndDelegate(LandEndDelegate, LandAttackLandMontage);

	// 落地段上的居合检测帧：此时人已在地面 → 由居合 GA 自己判成地面居合
	SetupWaitJuheCheck();
}

void UGA_Skill_02::StopLandAttackLoopMontage()
{
	UAnimInstance* AnimInst = GetOwnerAnimInstance();
	if (AnimInst && LandAttackLoopMontage && AnimInst->Montage_IsPlaying(LandAttackLoopMontage))
	{
		AnimInst->Montage_StopWithBlendOut(LandAttackLoopMontage->BlendOut, LandAttackLoopMontage);
	}
}

void UGA_Skill_02::OnLandAttackLandMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	K2_EndAbility();
}
      