#include "CountGauge.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/Image.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

void UCountGauge::NativeConstruct()
{
	Super::NativeConstruct();

	APawn* OwnerPawn = GetOwningPlayerPawn();
	if (!OwnerPawn) return;

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerPawn);
	if (!ASC) return;

	SetAndBoundToEnergyValue(ASC);
}

void UCountGauge::SetAndBoundToEnergyValue(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (!AbilitySystemComponent) return;

	OwnerASC = AbilitySystemComponent;

	// 初始化一次当前值
	const UExtraGameAttributeSet* AttrSet = OwnerASC->GetSet<UExtraGameAttributeSet>();
	const float InitialValue = AttrSet ? AttrSet->GetEnergyValue() : 0.f;
	if (ProgressImage)
	{
		ProgressImage->GetDynamicMaterial()->SetScalarParameterValue(PercentMaterialParamName, InitialValue);
	}

	OwnerASC->GetGameplayAttributeValueChangeDelegate(UExtraGameAttributeSet::GetEnergyValueAttribute())
		.AddUObject(this, &UCountGauge::UpdateGauge);
}

void UCountGauge::UpdateGauge(const FOnAttributeChangeData& Data)
{
	if (ProgressImage)
	{
		ProgressImage->GetDynamicMaterial()->SetScalarParameterValue(PercentMaterialParamName, Data.NewValue);
	}
}
