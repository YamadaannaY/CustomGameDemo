#include "ExtraAbilitySystemComponent.h"
#include "ExtraGameplayAbility.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameplayEffect.h"


void UExtraAbilitySystemComponent::ServerSideInit()
{
	InitializeBaseAttribute();
	ApplyInitialEffects();
	GiveInitialAbilities();
}

void UExtraAbilitySystemComponent::RemoveInnateAbilities()
{
	for (const FGameplayAbilitySpecHandle& Handle : InnateAbilityHandles)
	{
		if (Handle.IsValid())
		{
			ClearAbility(Handle);
		}
	}
	InnateAbilityHandles.Empty();

	for (const FActiveGameplayEffectHandle& Handle : InnateEffectHandles)
	{
		if (Handle.IsValid())
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	InnateEffectHandles.Empty();
}

void UExtraAbilitySystemComponent::InitializeBaseAttribute()
{
	if (!AttributeSetClass)
	{
		return;
	}

	UExtraGameAttributeSet* AttrSet = NewObject<UExtraGameAttributeSet>(GetOwner(), AttributeSetClass);
	if (AttrSet)
	{
		AddAttributeSetSubobject<UExtraGameAttributeSet>(AttrSet);
	}
}

void UExtraAbilitySystemComponent::ApplyInitialEffects()
{
	FGameplayEffectContextHandle ContextHandle = MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : InitEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1, ContextHandle);
		if (SpecHandle.IsValid())
		{
			FActiveGameplayEffectHandle ActiveHandle = ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			if (ActiveHandle.IsValid())
			{
				InnateEffectHandles.Add(ActiveHandle);
			}
		}
	}
}

void UExtraAbilitySystemComponent::GiveInitialAbilities()
{
	for (const auto& Pair : InnateActiveAbilities)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const int32 InputID = static_cast<int32>(Pair.Key);
		FGameplayAbilitySpecHandle Handle = GiveAbility(
			FGameplayAbilitySpec(Pair.Value, 1, InputID, this));
		InnateAbilityHandles.Add(Handle);
	}

	for (const TSubclassOf<UExtraGameplayAbility>& AbilityClass : InnatePassiveAbilities)
	{
		if (!AbilityClass)
		{
			continue;
		}
		FGameplayAbilitySpecHandle Handle = GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		InnateAbilityHandles.Add(Handle);
	}
}
