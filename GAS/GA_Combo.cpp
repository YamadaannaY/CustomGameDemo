#include "GA_Combo.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayTagsManager.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

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
	bEndingFromMovement = false;
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
}

void UGA_Combo::DoDamage(FGameplayEventData Data)
{
}