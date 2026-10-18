#include "AudioEventTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

void ExtraAudioEvent::ResolveProbability(const FAudioEventProbabilityCooldownInfo& Info,AActor* Owner, FGameplayTag& OutTag, float& OutProbability, float& OutCooldown)
{
	OutTag = FGameplayTag();
	OutProbability = Info.DefaultProbability;
	OutCooldown = Info.DefaultCooldownTime;

	const UAbilitySystemComponent* ASC = Owner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner) : nullptr;
	if (!ASC)
	{
		return;
	}

	for (const FAudioEventProbabilityInfo& Entry : Info.TagProbability)
	{
		if (Entry.Tag.IsValid() && ASC->HasMatchingGameplayTag(Entry.Tag))
		{
			OutTag = Entry.Tag;
			OutProbability = Entry.Probability;
			OutCooldown = Entry.CooldownTime;
			return;
		}
	}
}
