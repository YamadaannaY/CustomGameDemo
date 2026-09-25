

#include "OverHeadStatsGauge.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

void UOverHeadStatsGauge::ConfigureWithASC(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (AbilitySystemComponent)
	{
		HealthBar->SetAndBoundToGameplayAttribute(AbilitySystemComponent,UExtraGameAttributeSet::GetHealthAttribute(),UExtraGameAttributeSet::GetMaxHealthAttribute());
		HealthBar->SetAndBoundToShieldAttribute(AbilitySystemComponent);
		HealthBar->SetShieldFillColor(FLinearColor(1.0f, 0.8f, 0.0f));  // 金色护盾
	}
}
