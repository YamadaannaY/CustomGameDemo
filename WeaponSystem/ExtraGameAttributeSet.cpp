// Copyright Yu. All Rights Reserved.

#include "ExtraGameAttributeSet.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UExtraGameAttributeSet::UExtraGameAttributeSet()
{
	// 默认值（可被 GE 覆盖）
	Health = 100.f;
	MaxHealth = 100.f;
	AttackPower = 10.f;
	Stamina = 100.f;
	MaxStamina = 100.f;
	Shield = 0.f;
	ComboCount = 0.f;
}

void UExtraGameAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UExtraGameAttributeSet, Shield, COND_None, REPNOTIFY_Always);
}

void UExtraGameAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	
	if (bProcessingShieldAbsorption)
	{
		if (Attribute == GetHealthAttribute())
		{
			NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
		}
		if (Attribute == GetShieldAttribute())
		{
			NewValue = FMath::Max(NewValue, 0.f);
		}
		return;
	}

	if (Attribute == GetHealthAttribute())
	{
		const float OldHealth = GetHealth();
		const float CurrentShield = GetShield();

		if (NewValue < OldHealth && CurrentShield > 0.f)
		{
			const float DamageTaken = OldHealth - NewValue;
			const float Absorbed = FMath::Min(DamageTaken, CurrentShield);
			// 活跃 GE 对 Shield 的 Modifier = CurrentValue - BaseValue
			// SetShield() 会设 BaseValue=入参，导致 CurrentValue=BaseValue+GEModifier（叠加而非减少）
			// 正确做法：直接扣减 BaseValue
			const float ActiveGEModifier = CurrentShield - Shield.GetBaseValue();
			const float NewBaseValue = Shield.GetBaseValue() - Absorbed;

			Shield.SetBaseValue(NewBaseValue);
			Shield.SetCurrentValue(FMath::Max(0.f, NewBaseValue + ActiveGEModifier));

			NewValue = OldHealth - (DamageTaken - Absorbed);
		}

		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
}

void UExtraGameAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// GE 执行后再次 Clamp
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
	}
}

// ── Rep 回调 ──────────────────────────────────────────

void UExtraGameAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, Health, OldHealth);
}

void UExtraGameAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, MaxHealth, OldMaxHealth);
}

void UExtraGameAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, AttackPower, OldAttackPower);
}

void UExtraGameAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, Stamina, OldStamina);
}

void UExtraGameAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, MaxStamina, OldMaxStamina);
}

void UExtraGameAttributeSet::OnRep_Shield(const FGameplayAttributeData& OldShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UExtraGameAttributeSet, Shield, OldShield);
}
