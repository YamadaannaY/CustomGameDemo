#include "UExtraAbilitySystemStatic.h"

FGameplayTag UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag()
{
	return FGameplayTag::RequestGameplayTag("ability.basicattack");
}

FGameplayTag UUExtraAbilitySystemStatic::GetAirborneTag()
{
	return FGameplayTag::RequestGameplayTag("character.state.airborne");
}
