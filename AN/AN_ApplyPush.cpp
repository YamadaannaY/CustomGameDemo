#include "AN_ApplyPush.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayAbility.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

void UAN_ApplyPush::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	if (!ASC)
	{
		return;
	}

	// 把推力参数塞进 TargetData，随 Push_Self 事件发给当前激活 GA。
	FPushTargetData* PushData = new FPushTargetData();
	PushData->PushVelocity = PushVelocity;
	PushData->bOverrideXY = bOverrideXY;
	PushData->bOverrideZ = bOverrideZ;

	FGameplayEventData EventData;
	EventData.TargetData.Add(PushData);

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, UUExtraAbilitySystemStatic::GetPushSelfTag(), EventData);
}

FString UAN_ApplyPush::GetNotifyName_Implementation() const
{
	return TEXT("ApplyPush");
}
