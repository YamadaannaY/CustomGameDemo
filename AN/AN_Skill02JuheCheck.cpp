#include "AN_Skill02JuheCheck.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_Skill02JuheCheck::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetSkill02JuheCheckTag();
}

FString UAN_Skill02JuheCheck::GetNotifyName_Implementation() const
{
	return TEXT("Skill02JuheCheck");
}
