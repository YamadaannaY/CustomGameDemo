// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
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

	//根据队伍关系设置血条颜色：友方绿色，敌方红色
	void SetBarColorsByTeam(FGenericTeamId OwnerTeamID);
private:
	UPROPERTY(meta=(BindWidget))
	UValueGauge* HealthBar;
};
