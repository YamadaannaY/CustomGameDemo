#include "AN_EvadeToSprint.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/GAS/GA_Evade.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"

void UAN_EvadeToSprint::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character)
	{
		return;
	}

	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(Character);
	if (!PlayerChar)
	{
		return;
	}

	if (!PlayerChar->HasForwardInput())
	{
		return;
	}

	//触发Notify时将EventTag发送给Actor以触发WaitEventTask，进而触发Received回调
	if (! MeshComp->GetOwner()) return;

	UAbilitySystemComponent* OwnerASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
	if (!OwnerASC) return;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(MeshComp->GetOwner(),FGameplayTag::RequestGameplayTag("Evade.ToSprint"),FGameplayEventData());
}

FString UAN_EvadeToSprint::GetNotifyName_Implementation() const
{
	return TEXT("EvadeToSprint");
}
