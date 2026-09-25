
#pragma once

#include "CoreMinimal.h"
#include "ValueGauge.h"
#include "Blueprint/UserWidget.h"
#include "OverHeadStatsGauge.generated.h"

class UAbilitySystemComponent;
/**
 *
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UOverHeadStatsGauge : public UUserWidget
{
	GENERATED_BODY()

public:
	//为OverHeadBar调用SetAndBoundToGameplayAttribute，更新Percent和Text
	void ConfigureWithASC(UAbilitySystemComponent* AbilitySystemComponent);
private:
	UPROPERTY(meta=(BindWidget))
	UValueGauge* HealthBar;
};
