#include "AN_SendGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"


bool UAN_SendGameplayEvent::CanEditEventTag() const
{
	// 只有基类自身把 Tag 交给编辑器配置；特化子类都有自己的 GetEventTag
	return GetClass() == StaticClass();
}

void UAN_SendGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                   const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	//触发Notify时将Tag发送给Actor以触发WaitEventTask，进而触发Received回调
	if (!MeshComp || !MeshComp->GetOwner()) return;

	UAbilitySystemComponent* OwnerASC=UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
	if (!OwnerASC) return;

	FGameplayEventData EventData;
	BuildEventData(EventData, MeshComp);

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(MeshComp->GetOwner(),GetEventTag(),EventData);
}

FString UAN_SendGameplayEvent::GetNotifyName_Implementation() const
{
	const FGameplayTag Tag = GetEventTag();
	if (Tag.IsValid())
	{
		TArray<FName> TagNames;
		UGameplayTagsManager::Get().SplitGameplayTagFName( Tag, TagNames);

		return TagNames.Last().ToString();
	}
	return "None";
}
