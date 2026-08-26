// Fill out your copyright notice in the Description page of Project Settings.


#include "OverHeadStatsGauge.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "Kismet/GameplayStatics.h"

void UOverHeadStatsGauge::ConfigureWithASC(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (AbilitySystemComponent)
	{
		HealthBar->SetAndBoundToGameplayAttribute(AbilitySystemComponent,UExtraGameAttributeSet::GetHealthAttribute(),UExtraGameAttributeSet::GetMaxHealthAttribute());
		HealthBar->SetAndBoundToShieldAttribute(AbilitySystemComponent);
		HealthBar->SetShieldFillColor(FLinearColor(1.0f, 0.8f, 0.0f));  // 金色护盾
	}
}

void UOverHeadStatsGauge::SetBarColorsByTeam(FGenericTeamId OwnerTeamID)
{
	APawn* LocalPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!LocalPawn) return;

	IGenericTeamAgentInterface* LocalTeamAgent = Cast<IGenericTeamAgentInterface>(LocalPawn);
	if (!LocalTeamAgent) return;

	const bool bIsEnemy = LocalTeamAgent->GetGenericTeamId() != OwnerTeamID;

	if (bIsEnemy)
	{
		HealthBar->SetFillColor(FLinearColor::Red);
	}
	else
	{
		HealthBar->SetFillColor(FLinearColor::Green);
	}
}
