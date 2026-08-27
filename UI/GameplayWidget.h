// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "GameplayWidget.generated.h"

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGameplayWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	//为本地UIBar调用SetAndBoundToGameplayAttribute，更新Percent和Text
	virtual void NativeConstruct() override;
	
private:
	UPROPERTY(meta=(BindWidget))
	class UValueGauge* HealthBar;
	
	UPROPERTY(meta=(BindWidget))
	UValueGauge* StaminaBar;

	UPROPERTY(meta=(BindWidget))
	class UCountGauge* ComboGauge;
	
	UPROPERTY()
	UExtraAbilitySystemComponent* OwnerAbilitySystemComponent;
	
	//是否允许操控Pawn
	void SetOwningPawnInputEnabled(bool bPawnInputEnabled);
	
	//是否显示鼠标
	void SetShowMouseCursor(bool bShow);
	
	//允许操作UI和游戏，UI优先响应
	void SetFocusToGameAndUI();
	
	//只允许操作游戏
	void SetFocusToGameOnly();
};
