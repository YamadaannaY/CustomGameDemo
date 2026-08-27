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

	// 绑定 ComboCount attribute 变化委托，并初始化一次当前值
	void SetAndBoundToComboCount(UAbilitySystemComponent* AbilitySystemComponent);

private:
	UPROPERTY(EditDefaultsOnly,Category="Visual")
	FName PercentMaterialParamName="Count";

	UPROPERTY(meta=(BindWidget))
	class UImage* ProgressImage;

	UPROPERTY()
	UAbilitySystemComponent* OwnerASC;

	//属性变化后更新材质参数（Count）
	void UpdateGauge(const FOnAttributeChangeData& Data);
};
