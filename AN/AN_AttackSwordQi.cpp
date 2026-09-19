#include "AN_AttackSwordQi.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_AttackSwordQi::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetAirAttackSwordQiTag();
}

void UAN_AttackSwordQi::BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* /*MeshComp*/)
{
	// 没有 TargetData 类负载，倾斜角直接用 EventMagnitude
	OutEventData.EventMagnitude = RollAngle;
}

FString UAN_AttackSwordQi::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("SwordQi (%.0f)"), RollAngle);
}
