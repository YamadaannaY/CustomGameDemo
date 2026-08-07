// Fill out your copyright notice in the Description page of Project Settings.

#include "ExtraPlayerController.h"
#include "GAS/ExtraGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"


void AExtraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn)
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(InPawn))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				GrantInnateAbilities(ASC);
			}
		}
	}
}

void AExtraPlayerController::OnUnPossess()
{
	if (APawn* CurrentPawn = GetPawn())
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CurrentPawn))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				RemoveInnateAbilities(ASC);
			}
		}
	}

	Super::OnUnPossess();
}

void AExtraPlayerController::GrantInnateAbilities(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return;
	}

	// ── 主动 GA ──
	for (const auto& Pair : InnateActiveAbilities)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const int32 InputID = static_cast<int32>(Pair.Key);
		FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(
			FGameplayAbilitySpec(Pair.Value, 1, InputID, this));
		InnateAbilityHandles.Add(Handle);
	}

	// ── 被动 GA ──
	for (const TSubclassOf<UExtraGameplayAbility>& AbilityClass : InnatePassiveAbilities)
	{
		if (!AbilityClass)
		{
			continue;
		}
		FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		InnateAbilityHandles.Add(Handle);
	}

	// ── 初始 GE ──
	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : InitEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, 1, ContextHandle);
		if (SpecHandle.IsValid())
		{
			FActiveGameplayEffectHandle ActiveHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			if (ActiveHandle.IsValid())
			{
				InnateEffectHandles.Add(ActiveHandle);
			}
		}
	}
}

void AExtraPlayerController::RemoveInnateAbilities(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : InnateAbilityHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}
	InnateAbilityHandles.Empty();

	for (const FActiveGameplayEffectHandle& Handle : InnateEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	InnateEffectHandles.Empty();
}
