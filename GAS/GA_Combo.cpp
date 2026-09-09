#include "GA_Combo.h"
#include "GameplayTagsManager.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Combo::UGA_Combo() : ComboMontage(nullptr)
{
	//为了在GA内不重复触发GA，且在空中时不激活地面 Combo
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAirborneTag());

	// 霸体期间（SkillGA 表现段）不可激活；后摇段放开后，激活时取消 SkillGA 打断其后摇。
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());
	
	// 启用移动打断（基类机制）
	bEnableMovementCancel = true;

	// 启用通用武器碰撞伤害（基类机制）：服务端监听命中事件并按 Section 应用 GE
	bEnableWeaponDamage = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

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

		//派生类可在此追加注册 Montage 额外事件监听（形态一重击：末段累计 / 重击切入帧判定）
		SetupComboMontageListeners();

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

TSubclassOf<UGameplayEffect> UGA_Combo::GetDamageEffect() const
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
	return DefaultWeaponDamageEffect;
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

	// 仅通知派生类已进入下一段：父类默认手动等下次输入推进；按住自动续段由派生类覆写实现
	OnComboSectionChanged();
}

void UGA_Combo::OnComboSectionChanged()
{
	// 父类默认手动节奏：进入下一段后不自动推进，等下一次攻击输入触发 TryCommitCombo
}

void UGA_Combo::SetupComboMontageListeners()
{
	// 基类纯连段不注册额外监听；派生类（UGA_ComboHeavy）在此覆写注册末段累计 / 重击切入帧判定
}