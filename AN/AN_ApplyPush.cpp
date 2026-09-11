#include "AN_ApplyPush.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_ApplyPush::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetPushSelfTag();
}

void UAN_ApplyPush::BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* /*MeshComp*/)
{
	// 把推力参数塞进 TargetData，随 Push_Self 事件发给当前激活 GA
	FPushTargetData* PushData = new FPushTargetData();
	PushData->PushVelocity = PushVelocity;
	PushData->bOverrideXY = bOverrideXY;
	PushData->bOverrideZ = bOverrideZ;

	OutEventData.TargetData.Add(PushData);
}

FString UAN_ApplyPush::GetNotifyName_Implementation() const
{
	return TEXT("ApplyPush");
}
