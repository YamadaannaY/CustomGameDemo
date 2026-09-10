#include "GA_HeavyAttackCharge.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "GameplayEffect.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_HeavyAttackCharge::UGA_HeavyAttackCharge()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag());
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetPhase2StateTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetUninterruptibleTag());
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());
	
	bEnableUninterruptible = true;
	
	bEnableWeaponDamage = true;

	bRotateToLockTarget = true;

	FAbilityTriggerData HeavyAttackTrigger;
	HeavyAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	HeavyAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetHeavyAttackInputTag();
	AbilityTriggers.Add(HeavyAttackTrigger);
}

void UGA_HeavyAttackCharge::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bInEnding = false;
	bLoopStarted = false;
	CurrentPlayingMontage = nullptr;
	ChargeStaminaDrainHandle.Invalidate();

	if (!ChargeStartMontage || !ChargeLoopMontage || !ChargeEndMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_HeavyAttackCharge] 三段 Montage 未配置完整，取消激活"));
		K2_EndAbility();
		return;
	}

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* StartTask = PlayMontage(ChargeStartMontage);
		//进入循环段
		StartTask->OnBlendOut.AddDynamic(this, &ThisClass::OnStartMontageCompleted);
		StartTask->OnCompleted.AddDynamic(this, &ThisClass::OnStartMontageCompleted);
		//结束GA
		StartTask->OnInterrupted.AddDynamic(this, &ThisClass::OnStartMontageInterrupted);
		StartTask->OnCancelled.AddDynamic(this, &ThisClass::OnStartMontageInterrupted);
		
		StartTask->ReadyForActivation();

		// GA 结束时 Task 自动清理，不会影响下一次激活
		UAbilityTask_WaitGameplayEvent* WaitReleaseTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, UUExtraAbilitySystemStatic::GetHeavyAttackReleaseInputTag(), nullptr, true, true);
		WaitReleaseTask->EventReceived.AddDynamic(this, &ThisClass::HandleRelease);
		WaitReleaseTask->ReadyForActivation();
	}
}

void UGA_HeavyAttackCharge::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                       bool bReplicateEndAbility, bool bWasCancelled)
{
	CurrentPlayingMontage = nullptr;
	
	// 兜底停掉仍在播的段（异常结束 / 被打断路径）
	StopCurrentPlayingMontage();

	// 兜底移除持续消耗（正常路径已在 EnterEndPhase 移除）
	RemoveStaminaDrain();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAbilityTask_PlayMontageAndWait* UGA_HeavyAttackCharge::PlayMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return nullptr;
	}

	CurrentPlayingMontage = Montage;
	
	// 该任务会一直播放、不产生完成回调，直到被StopCurrentPlayingMontage停掉
	return UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage);
}

void UGA_HeavyAttackCharge::StopCurrentPlayingMontage()
{
	UAnimInstance* Anim = GetOwnerAnimInstance();
	if (Anim && CurrentPlayingMontage && Anim->Montage_IsPlaying(CurrentPlayingMontage))
	{
		Anim->Montage_Stop(-1, CurrentPlayingMontage);
	}
}

void UGA_HeavyAttackCharge::EnterEndPhase()
{
	// 防重入，GA只可以进一次结束段
	if (bInEnding)
	{
		return;
	}
	bInEnding = true;

	// 打出攻击即停止蓄力消耗
	RemoveStaminaDrain();

	// 先停当前段（起手或循环），避免与结束段争同一 Slot。
	// 必须在 bInEnding 置位之后再 Stop——Stop 会同步触发起手段的 Interrupted 回调。
	StopCurrentPlayingMontage();

	UAbilityTask_PlayMontageAndWait* EndTask = PlayMontage(ChargeEndMontage);
	if (!EndTask)
	{
		K2_EndAbility();
		return;
	}

	EndTask->OnCompleted.AddDynamic(this, &ThisClass::OnEndMontageFinished);
	EndTask->OnBlendOut.AddDynamic(this, &ThisClass::OnEndMontageFinished);
	EndTask->OnInterrupted.AddDynamic(this, &ThisClass::OnEndMontageFinished);
	EndTask->OnCancelled.AddDynamic(this, &ThisClass::OnEndMontageFinished);
	EndTask->ReadyForActivation();
}

void UGA_HeavyAttackCharge::HandleRelease(FGameplayEventData EventData)
{
	// GA 激活后任意时刻松手 → 立即停当前段、播结束段打出攻击
	EnterEndPhase();
	
	UE_LOG(LogTemp,Warning,TEXT("[GA_HeavyAttackCharge] 输入松手，打出重击"));
}

void UGA_HeavyAttackCharge::OnStartMontageCompleted()
{
	if (bInEnding || bLoopStarted)
	{
		return;
	}
	bLoopStarted = true;

	//应用持续消耗耐力的 GE
	ApplyStaminaDrain();
	
	//进入循环段
	if (UAbilityTask_PlayMontageAndWait* LoopTask = PlayMontage(ChargeLoopMontage))
	{
		LoopTask->ReadyForActivation();
	}
}

void UGA_HeavyAttackCharge::ApplyStaminaDrain()
{
	if (!K2_HasAuthority() || !ChargeStaminaDrainEffect)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	// ChargeStaminaDrainEffect 应为 Infinite + Period 的周期 GE，应用后持续生效，直到 RemoveStaminaDrain 显式移除。
	const UGameplayEffect* DrainCDO = ChargeStaminaDrainEffect->GetDefaultObject<UGameplayEffect>();
	ChargeStaminaDrainHandle = ASC->ApplyGameplayEffectToSelf(DrainCDO, GetAbilityLevel(), Context);
}

void UGA_HeavyAttackCharge::RemoveStaminaDrain()
{
	if (!ChargeStaminaDrainHandle.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveActiveGameplayEffect(ChargeStaminaDrainHandle);
	}

	ChargeStaminaDrainHandle.Invalidate();
}

void UGA_HeavyAttackCharge::OnStartMontageInterrupted()
{
	if (!bInEnding)
	{
		K2_EndAbility();
	}
}

void UGA_HeavyAttackCharge::OnEndMontageFinished()
{
	K2_EndAbility();
}
