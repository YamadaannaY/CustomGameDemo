#include "AN_EvadeToSprint.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
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

	// 无条件发送事件，由 GA 负责在 Notify 之后持续检测输入
	if (!MeshComp->GetOwner()) return;

	UAbilitySystemComponent* OwnerASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
	if (!OwnerASC) return;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(MeshComp->GetOwner(), UUExtraAbilitySystemStatic::GetEvadeToSprintTag(), FGameplayEventData());
}

FString UAN_EvadeToSprint::GetNotifyName_Implementation() const
{
	return TEXT("EvadeToSprint");
}
