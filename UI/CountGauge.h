#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "CountGauge.generated.h"

/**
 * 计数相关逻辑UI显示，可自由调整计数值
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UCountGauge : public UUserWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeConstruct() override;

	// 绑定 EnergyValue attribute 变化委托，并初始化一次当前值
	void SetAndBoundToEnergyValue(UAbilitySystemComponent* AbilitySystemComponent);

private:
	UPROPERTY(EditDefaultsOnly,Category="Visual")
	FName PercentMaterialParamName="Count";

	UPROPERTY(meta=(BindWidget))
	class UImage* ProgressImage;

	UPROPERTY(meta=(BindWidgetOptional))
	UImage* Icon1;

	UPROPERTY(meta=(BindWidgetOptional))
	UImage* Icon2;

	UPROPERTY(meta=(BindWidgetOptional))
	UImage* Icon3;

	// 每格所需的能量值（100 / 200 / 300）
	UPROPERTY(EditDefaultsOnly, Category="Visual")
	float IconUnlockStep = 100.f;

	UPROPERTY()
	UAbilitySystemComponent* OwnerASC;

	//属性变化后更新材质参数（Count）
	void UpdateGauge(const FOnAttributeChangeData& Data);

	// 按当前能量刷新三个图标的显隐
	void RefreshIcons(float Energy);
};
