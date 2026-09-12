#include "GA_Combo_Phase_2.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

void UGA_Combo_Phase_2::DoDamage(const FGameplayEventData& Data)
{
	Super::DoDamage(Data);

	if (!K2_HasAuthority())
	{
		return;
	}
	
	// 攻击窗口空挥（未命中任何目标）不累积能量
	const TArray<AActor*> HitActors = UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(Data.TargetData);

	// 攻击窗口空挥（未命中任何目标）不累积能量
	if (HitActors.Num() == 0)
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
