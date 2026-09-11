#include "AN_EndUninterruptible.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_EndUninterruptible::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetUninterruptibleEndTag();
}

FString UAN_EndUninterruptible::GetNotifyName_Implementation() const
{
	return TEXT("EndUninterruptible");
}
