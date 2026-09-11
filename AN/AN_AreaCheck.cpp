#include "AN_AreaCheck.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_AreaCheck::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetAreaDamageTag();
}

void UAN_AreaCheck::BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* /*MeshComp*/)
{
	FAreaCheckData* AreaData = new FAreaCheckData();
	AreaData->CenterMode = CenterMode;
	AreaData->CenterOffset = CenterOffset;
	AreaData->Radius = Radius;

	OutEventData.TargetData.Add(AreaData);
}
