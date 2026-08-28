#include "AN_EndUninterruptible.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

void UAN_EndUninterruptible::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, UUExtraAbilitySystemStatic::GetUninterruptibleEndTag(), FGameplayEventData());
}

FString UAN_EndUninterruptible::GetNotifyName_Implementation() const
{
	return TEXT("EndUninterruptible");
}
