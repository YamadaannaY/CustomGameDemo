#include "GA_Skill_02.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Character.h"
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

	// 表现动画段挂 State.Uninterruptible，后摇段由 AN_EndUninterruptible 放开
	bEnableUninterruptible = true;

	// 启用通用武器碰撞伤害（基类机制）
	bEnableWeaponDamage = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

	// 落地斩属于空中下落类攻击：落点必须拉出目标胶囊，否则会先被水平拖到目标正上方、
	// 再垂直落到胶囊顶面并顺坡滑走。取值与 GA_AirAttack 一致。
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
	// 那样会把「还剩 1 层充能」也一并封死。这里改成按层数放行：
	// cooldown GE 的 stack 数 = 已消耗、正在回充的层数，用满才对激活说 no。
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

	if (UsedCharges < UUExtraAbilitySystemStatic::Skill02MaxCharges)
	{
		return true;
	}

	// 层数用满：沿用原生的失败约定，把命中的 cooldown tag 回填给调用方做诊断
	if (OptionalRelevantTags)
	{
		OptionalRelevantTags->AppendMatchingTags(ASC->GetOwnedGameplayTags(), *CooldownTags);
	}
	return false;
}

void UGA_Skill_02::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 充能的检查与消耗交给 GAS：CheckCooldown（本类 override）已挡住层数用满的情况，
	// 提交成功则给 cooldown GE 叠一层。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo, nullptr))
	{
		K2_EndAbility();
		return;
	}

	CurrentPhase = ESkill02Phase::None;
	bRiseReadyMarked = false;
	bPendingSkillInput = false;

	if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		return;
	}

	// 表现按角色当前位置分派：空中 → 落地斩；地面 → 升空斩
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

	// 解锁移动输入（段1 / 段2 起手与循环段会锁上）
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

// ── 段1：升空斩 ──────────────────────────────────────────────

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
	// 必须延迟到下一帧再挂：AbilityTriggers 的激活就发生在 HandleGameplayEvent 内部，
	// 而该函数随后会用「已包含刚注册回调」的委托表广播同一个事件——当场挂会被这次按键立刻触发，
	// 表现为刚起手就切落地斩（在地面时更是直接播落地段），同时还白扣一层充能。
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::SetupWaitSkillInput);
	}

	// 段1 的衔接标记帧（Montage 里的 AN_Skill02RiseReady）：收到才允许切落地斩。
	// 与输入监听不同，本事件由动画帧发出，不可能在激活同帧到达，因此无需错开一帧。
	UAbilityTask_WaitGameplayEvent* WaitRiseReadyTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetSkill02RiseReadyTag(), nullptr, /*OnlyTriggerOnce=*/true, /*OnlyMatchExact=*/true);
	WaitRiseReadyTask->EventReceived.AddDynamic(this, &ThisClass::OnRiseReadyMarked);
	WaitRiseReadyTask->ReadyForActivation();
}

void UGA_Skill_02::OnRiseReadyMarked(FGameplayEventData Payload)
{
	// 升空已到位，此后（且仍在空中）的第二下技能输入才切落地斩；
	// 若玩家在标记帧之前就按过 E，这里补一次判定，避免那次输入白按。
	bRiseReadyMarked = true;
	TryEnterLandAttack();
}

void UGA_Skill_02::SetupWaitSkillInput()
{
	// 一帧之内 GA 可能已经结束（被打断 / 段1 已播完），此时不再挂监听
	if (!IsActive() || CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}

	// OnlyTriggerOnce = false：早于标记帧按下的那次输入不能被消费掉（任务一旦结束就再收不到），
	// 「只切一次」由 CurrentPhase 保证——切进段2 后 Phase 已不是 Rise，回调直接 return。
	UAbilityTask_WaitGameplayEvent* WaitSkillInputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetSkillInputTag(), nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	WaitSkillInputTask->EventReceived.AddDynamic(this, &ThisClass::OnSkillInputDuringRise);
	WaitSkillInputTask->ReadyForActivation();
}

void UGA_Skill_02::OnRiseMontageFinished()
{
	if (CurrentPhase != ESkill02Phase::Rise)
	{
		return;
	}

	K2_EndAbility();
}

void UGA_Skill_02::OnRiseMontageInterrupted()
{
	// 切段2 时会主动停段1，那时 CurrentPhase 已离开 Rise，此处直接 return，不误结束 GA
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

	// 只记录「段1 期间按过 E」：此刻能不能切由 TryEnterLandAttack 判定。
	// 早于标记帧按下的那次会留到标记帧到达时补触发，所以快速双击的第二下不会白按。
	bPendingSkillInput = true;
	TryEnterLandAttack();
}

void UGA_Skill_02::TryEnterLandAttack()
{
	if (CurrentPhase != ESkill02Phase::Rise || !bRiseReadyMarked || !bPendingSkillInput)
	{
		return;
	}

	// 必须仍在空中：已经落回地面时不再衔接落地斩
	// （那时等段1 播完，由「按位置分派」决定下一次按 E 播哪段）
	const ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar || !AvatarChar->GetCharacterMovement() || !AvatarChar->GetCharacterMovement()->IsFalling())
	{
		return;
	}

	// 消耗第二层充能：K2_CommitAbilityCooldown 内部会先跑本类 override 的 CheckCooldown，
	// 两层都在回充时返回 false —— 不扣层、也不切段。
	if (!K2_CommitAbilityCooldown(/*BroadcastCommitEvent=*/false, /*ForceCooldown=*/false))
	{
		return;
	}

	bPendingSkillInput = false;
	EnterLandAttack();
}

// ── 段2：落地斩（起手 → 循环 → 落地，衔接逻辑同 GA_AirAttack）──────

void UGA_Skill_02::EnterLandAttack()
{
	// 先切阶段再停段1：Montage_Stop 会同步触发段1 的 Interrupted 回调，
	// 那时 CurrentPhase 已不是 Rise，回调会直接 return。
	CurrentPhase = ESkill02Phase::LandStart;

	if (UAnimInstance* AnimInst = GetOwnerAnimInstance())
	{
		if (RiseMontage && AnimInst->Montage_IsPlaying(RiseMontage))
		{
			AnimInst->Montage_StopWithBlendOut(RiseMontage->BlendOut, RiseMontage);
		}
	}

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

	// 从起手段就监听落地：低空触发时段2 可能在起手还没播完时就已经落地
	if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarChar->LandedDelegate.AddDynamic(this, &UGA_Skill_02::OnLandDetected);
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(LandCheckTimerHandle, this, &UGA_Skill_02::PollLandCheck, 0.08f, true);
	}

	// 用 PlayMontageAndWait 的 BlendOut（开始淡出）就接循环段，让两段淡化重叠，避免真空帧掉回状态机
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
	// 只有起手段被真正外部打断才结束 GA；进循环/落地后主动停起手属于正常流程
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
	// 用 Montage 资产里配的 BlendIn，代码不写死混出时间
	AnimInst->Montage_Play(LandAttackLoopMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);

	// section 自循环：不依赖资产是否勾了 bLoop，确保下砸循环播完跳回自身
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
	// 起手与循环两段都可能落地（低空触发时起手就可能已在落地）
	if (CurrentPhase != ESkill02Phase::LandStart && CurrentPhase != ESkill02Phase::LandLoop)
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
