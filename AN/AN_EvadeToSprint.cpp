#include "AN_EvadeToSprint.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

FGameplayTag UAN_EvadeToSprint::GetEventTag() const
{
	return UUExtraAbilitySystemStatic::GetEvadeToSprintTag();
}

void UAN_EvadeToSprint::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	// 仅玩家角色触发，由 GA 负责在 Notify 之后持续检测输入
	if (!MeshComp || !Cast<AExtraPlayerCharacter>(MeshComp->GetOwner()))
	{
		return;
	}

	Super::Notify(MeshComp, Animation, EventReference);
}

FString UAN_EvadeToSprint::GetNotifyName_Implementation() const
{
	return TEXT("EvadeToSprint");
}
