// Fill out your copyright notice in the Description page of Project Settings.

#include "GameplayWidget.h"
#include "ExtractGameCharacter/UI/ValueGauge.h"
#include "AbilitySystemBlueprintLibrary.h"
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
	}
	
	SetShowMouseCursor(false);
	SetFocusToGameOnly();
	
	
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
