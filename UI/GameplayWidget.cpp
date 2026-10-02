// Fill out your copyright notice in the Description page of Project Settings.

#include "GameplayWidget.h"
#include "ExtractGameCharacter/UI/ValueGauge.h"
#include "ExtractGameCharacter/UI/CountGauge.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

void UGameplayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	OwnerAbilitySystemComponent=Cast<UExtraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwningPlayerPawn()));
	
	if (OwnerAbilitySystemComponent)
	{
		HealthBar->SetAndBoundToGameplayAttribute(OwnerAbilitySystemComponent,UExtraGameAttributeSet::GetHealthAttribute(),UExtraGameAttributeSet::GetMaxHealthAttribute());
		HealthBar->SetAndBoundToShieldAttribute(OwnerAbilitySystemComponent);
		HealthBar->SetShieldFillColor(FLinearColor(1.0f, 0.8f, 0.0f));  // 金色护盾
		StaminaBar->SetAndBoundToGameplayAttribute(OwnerAbilitySystemComponent,UExtraGameAttributeSet::GetStaminaAttribute(),UExtraGameAttributeSet::GetMaxStaminaAttribute());
		ComboGauge->SetAndBoundToEnergyValue(OwnerAbilitySystemComponent);

		//连段能量文本：两个属性任一变化都重算「当前值/最大值」
		OwnerAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UExtraGameAttributeSet::GetEnergyValueAttribute())
			.AddUObject(this,&UGameplayWidget::ComboEnergyChanged);
		OwnerAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UExtraGameAttributeSet::GetEnergyMaxValueAttribute())
			.AddUObject(this,&UGameplayWidget::ComboEnergyChanged);
		UpdateComboEnergyText();
	}

	SetShowMouseCursor(false);
	SetFocusToGameOnly();
	
}

void UGameplayWidget::UpdateComboEnergyText()
{
	if (!ComboValueText || !OwnerAbilitySystemComponent) return;

	const float CurrentEnergy = OwnerAbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
	const float MaxEnergy = OwnerAbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyMaxValueAttribute());

	//与 ValueGauge 的数值文本保持一致：无小数、当前值/最大值
	const FNumberFormattingOptions FormatOps = FNumberFormattingOptions().SetMaximumFractionalDigits(0);
	ComboValueText->SetText(FText::Format(
		FTextFormat::FromString(TEXT("{0}/{1}")),
		FText::AsNumber(CurrentEnergy, &FormatOps),
		FText::AsNumber(MaxEnergy, &FormatOps)
	));
}

void UGameplayWidget::ComboEnergyChanged(const FOnAttributeChangeData& Data)
{
	UpdateComboEnergyText();
}

void UGameplayWidget::SetOwningPawnInputEnabled(bool bPawnInputEnabled)
{
	if (bPawnInputEnabled)
	{
		GetOwningPlayerPawn()->EnableInput(GetOwningPlayer());
	}
	else 
	{
		GetOwningPlayerPawn()->DisableInput(GetOwningPlayer());
	}
}

void UGameplayWidget::SetShowMouseCursor(bool bShow)
{
	GetOwningPlayer()->SetShowMouseCursor(bShow);
}

void UGameplayWidget::SetFocusToGameAndUI()
{
	FInputModeGameAndUI GameAndUIInputMode;
	//不因Capture下Mousedown操作而隐藏鼠标
	GameAndUIInputMode.SetHideCursorDuringCapture(false);
	
	GetOwningPlayer()->SetInputMode(GameAndUIInputMode);
}

void UGameplayWidget::SetFocusToGameOnly()
{
	FInputModeGameOnly GameOnlyInputMode;
	GetOwningPlayer()->SetInputMode(GameOnlyInputMode);
}
