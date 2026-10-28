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
	RefreshIcons(InitialValue);

	OwnerASC->GetGameplayAttributeValueChangeDelegate(UExtraGameAttributeSet::GetEnergyValueAttribute())
		.AddUObject(this, &UCountGauge::UpdateGauge);
}

void UCountGauge::UpdateGauge(const FOnAttributeChangeData& Data)
{
	if (ProgressImage)
	{
		ProgressImage->GetDynamicMaterial()->SetScalarParameterValue(PercentMaterialParamName, Data.NewValue);
	}

	RefreshIcons(Data.NewValue);
}

void UCountGauge::RefreshIcons(float Energy)
{
	// 满一格亮一个：100 / 200 / 300
	if (Icon1)
	{
		Icon1->SetVisibility(Energy >= IconUnlockStep
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}
	if (Icon2)
	{
		Icon2->SetVisibility(Energy >= IconUnlockStep * 2.f
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}
	if (Icon3)
	{
		Icon3->SetVisibility(Energy >= IconUnlockStep * 3.f
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Hidden);
	}
}
