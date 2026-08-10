#include "UExtraAbilitySystemStatic.h"

FGameplayTag UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag()
{
	return FGameplayTag::RequestGameplayTag("ability.basicattack");
}
