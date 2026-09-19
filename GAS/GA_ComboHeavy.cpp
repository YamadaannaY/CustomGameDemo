#include "GA_ComboHeavy.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

UGA_ComboHeavy::UGA_ComboHeavy()
{
}

FName UGA_ComboHeavy::GetComboStartSectionName()
{
	// 跨激活重置（InstancedPerActor 实例复用）
	bSkill01ComboBoost = false;

	AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerCharacter || !PlayerCharacter->ConsumeSkill01ComboBoost())
	{
		return NAME_None;
	}

	// Skill01 强化：本次普攻跳过前两段，直接从末段（+100 能量那段）起播
	bSkill01ComboBoost = true;
	return GetLastComboSectionName();
}

void UGA_ComboHeavy::SetupComboMontageListeners()
{
	// 监听进入最后一段 section（最后段第一帧的 Notify 发送），累计能量。
	UAbilityTask_WaitGameplayEvent* WaitLastSectionTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetComboLastSectionTag(), nullptr, false, true);
	WaitLastSectionTask->EventReceived.AddDynamic(this, &ThisClass::OnLastSectionEntered);
	WaitLastSectionTask->ReadyForActivation();

	// 监听最后一段的切入帧 Notify：EnergyValue 打满且长按达标时，切入重击。
	UAbilityTask_WaitGameplayEvent* WaitHeavyTransitionTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetComboHeavyTransitionTag(), nullptr, false, true);
	WaitHeavyTransitionTask->EventReceived.AddDynamic(this, &ThisClass::OnHeavyTransitionFrame);
	WaitHeavyTransitionTask->ReadyForActivation();
}

void UGA_ComboHeavy::OnComboSectionChanged()
{
	// 本形态仍按住攻击键则自动续段（父类默认手动，自动连段只在此具体形态开启）
	if (IsHoldingAttack())
	{
		TryCommitCombo();
	}
}

bool UGA_ComboHeavy::IsHoldingAttack() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter && PlayerCharacter->IsHoldingAttack();
}

void UGA_ComboHeavy::OnLastSectionEntered(FGameplayEventData EventData)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	// 本段基础 +100；Skill01 强化时额外再 +100（封顶由属性集按 EnergyMaxValue 处理）
	const float EnergyGain = bSkill01ComboBoost ? 200.f : 100.f;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue,
			FString::Printf(TEXT("打出第三段普攻，获得%g点心念"), EnergyGain));
	}
	const float CurrentEnergyValue = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());

	ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetEnergyValueAttribute(), CurrentEnergyValue + EnergyGain);
}

void UGA_ComboHeavy::OnHeavyTransitionFrame(FGameplayEventData EventData)
{
	// 「长按达到重击阈值」
	if (!IsLongPressed())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	const float EnergyValue = ASC->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
	if (EnergyValue < GetRequiredComboCount())
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

bool UGA_ComboHeavy::IsLongPressed() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter && PlayerCharacter->IsLongPressed();
}

// 重击所需能量值（经 Character 读属性集 EnergyMaxValue）
float UGA_ComboHeavy::GetRequiredComboCount() const
{
	const AExtraPlayerCharacter* PlayerCharacter = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	return PlayerCharacter ? PlayerCharacter->GetHeavyComboEnergyNeed() : 300.f;
}
