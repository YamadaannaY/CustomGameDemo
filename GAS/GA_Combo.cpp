#include "GA_Combo.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayTagsManager.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"

UGA_Combo::UGA_Combo() : ComboMontage(nullptr)
{
	//为了在GA内不重复触发GA，且在空中时不激活地面 Combo
	AbilityTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());

	// 启用移动打断（基类机制）
	bEnableMovementCancel = true;

	// 通过 InputTag 触发
	FAbilityTriggerData LightAttackTrigger;
	LightAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	LightAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
	AbilityTriggers.Add(LightAttackTrigger);
}

void UGA_Combo::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (! K2_CommitAbility())
	{
		K2_EndAbility();
		return ;
	}

	// 重置跨激活状态（InstancedPerActor 实例复用，避免残留到下一次激活）
	NextComboName = NAME_None;

	if (HasAuthorityOrPredictionKey(ActorInfo,&ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* PlayComboMontageTask=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,ComboMontage);
		PlayComboMontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
		PlayComboMontageTask->ReadyForActivation();

		//接收Notify的EventTag
		UAbilityTask_WaitGameplayEvent* WaitComboChangeEventTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GetComboChangedEventTag(),nullptr,false,false);
		WaitComboChangeEventTask->EventReceived.AddDynamic(this,&ThisClass::ComboChangedEventReceived);
		WaitComboChangeEventTask->ReadyForActivation();

		//监听进入最后一段 section（最后段第一帧的 Notify 发送），累计「打满」次数。
		// OnlyTriggerOnce=false：连段循环（最后一段跳回第一段）时，每次进入最后一段都要 +1。
		UAbilityTask_WaitGameplayEvent* WaitLastSectionTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,UUExtraAbilitySystemStatic::GetComboLastSectionTag(),nullptr,false,true);
		WaitLastSectionTask->EventReceived.AddDynamic(this,&ThisClass::OnLastSectionEntered);
		WaitLastSectionTask->ReadyForActivation();

		//监听最后一段的切入帧 Notify：ComboCount 打满段数且按住攻击键时，切入重击。
		UAbilityTask_WaitGameplayEvent* WaitHeavyTransitionTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,UUExtraAbilitySystemStatic::GetComboHeavyTransitionTag(),nullptr,false,true);
		WaitHeavyTransitionTask->EventReceived.AddDynamic(this,&ThisClass::OnHeavyTransitionFrame);
		WaitHeavyTransitionTask->ReadyForActivation();

	}

	//在服务端实现Damage逻辑
	if (K2_HasAuthority())
	{
		UAbilityTask_WaitGameplayEvent* WaitTargetEventTask=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GetComboTargetEventTag());
		WaitTargetEventTask->EventReceived.AddDynamic(this,&ThisClass::DoDamage);
		WaitTargetEventTask->ReadyForActivation();

	}

	//处理第一次输入
	SetupWaitComboInputPress();
}

FGameplayTag UGA_Combo::GetComboChangedEventTag()
{
	return UUExtraAbilitySystemStatic::GetComboChangedEventTag();
}

FGameplayTag UGA_Combo::GetComboChangedEventEndTag()
{
	return UUExtraAbilitySystemStatic::GetComboChangedEventEndTag();
}

FGameplayTag UGA_Combo::GetComboTargetEventTag()
{
	return UUExtraAbilitySystemStatic::GetComboTargetEventTag();
}

void UGA_Combo::HandleInputPress(FGameplayEventData EventData)
{
	SetupWaitComboInputPress();
	TryCommitCombo();
}

void UGA_Combo::SetupWaitComboInputPress()
{
	// 废弃 WaitInputPress，改为监听 LightAttack InputTag 的 GameplayEvent
	UAbilityTask_WaitGameplayEvent* WaitComboInputTag = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetLightAttackInputTag(), nullptr, true, false);
	WaitComboInputTag->EventReceived.AddDynamic(this, &ThisClass::HandleInputPress);
	WaitComboInputTag->ReadyForActivation();
}

void UGA_Combo::TryCommitCombo()
{
	if (NextComboName==NAME_None) return;

	UAnimInstance* OwnerAnimInst=GetOwnerAnimInstance();
	if (!OwnerAnimInst) return;

	//设置当前Montage Section的NextSection
	OwnerAnimInst->Montage_SetNextSection(OwnerAnimInst->Montage_GetCurrentSection(ComboMontage),NextComboName,ComboMontage);
	OwnerAnimInst->Montage_JumpToSection(NextComboName, ComboMontage);

	NextComboName=NAME_None;
}

TSubclassOf<UGameplayEffect> UGA_Combo::GetDamageEffectForCurrentCombo() const
{
	if (UAnimInstance* OwnerAnimInst=GetOwnerAnimInstance())
	{
		const FName CurrentSectionName=OwnerAnimInst->Montage_GetCurrentSection(ComboMontage);
		const TSubclassOf<UGameplayEffect>* FoundEffectPtr=DamageEffectMap.Find(CurrentSectionName);
		if (FoundEffectPtr)
		{
			return *FoundEffectPtr;
		}
	}
	return DefaultDamageEffect;
}

void UGA_Combo::ComboChangedEventReceived(FGameplayEventData InPayLoad)
{
	const FGameplayTag EventTag=InPayLoad.EventTag;

	if (EventTag==GetComboChangedEventEndTag())
	{
		//此时到Section末尾，需要重置NextComboName
		NextComboName=NAME_None;
		return;
	}

	TArray<FName> TagNames;
	UGameplayTagsManager::Get().SplitGameplayTagFName(EventTag, TagNames);
	NextComboName=TagNames.Last();

	// 仍然按住攻击键时，自动跳转到下一段，实现自动连段
	if (IsHoldingAttack())
	{
		TryCommitCombo();
	}
}

void UGA_Combo::DoDamage(FGameplayEventData Data)
{
}

void UGA_Combo::OnLastSectionEntered(FGameplayEventData EventData)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	const float Current = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetComboCountAttribute());
	ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetComboCountAttribute(), FMath::Min(Current + 1.f, GetRequiredComboCount()));
}

void UGA_Combo::OnHeavyTransitionFrame(FGameplayEventData EventData)
{
	// 重击需「长按达到阈值」：高频点按即使段数满也不触发
	if (!IsLongPressed())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	const float ComboCount = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetComboCountAttribute());
	if (ComboCount < GetRequiredComboCount())
	{
		return;
	}

	// 切入帧：发送重击输入事件，触发重击 GA，并结束当前轻击 GA
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		GetAvatarActorFromActorInfo(),
		UUExtraAbilitySystemStatic::GetHeavyAttackInputTag(),
		FGameplayEventData());

	K2_EndAbility();
}

bool UGA_Combo::IsHoldingAttack() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter && PlayerCharacter->IsHoldingAttack();
}

bool UGA_Combo::IsLongPressed() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter && PlayerCharacter->IsLongPressed();
}

float UGA_Combo::GetRequiredComboCount() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter ? PlayerCharacter->GetHeavyComboCount() : 3.f;
}