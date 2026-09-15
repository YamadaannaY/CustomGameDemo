#include "AN_GameplayEventWindow.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

bool UAN_GameplayEventWindow::CanEditWindowTags() const
{
	// 只有基类自身把 Tag 交给编辑器配置；特化子类具体定义自身对应的 GetWindowXXXTag
	return GetClass() == StaticClass();
}

void UAN_GameplayEventWindow::SendWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag) const
{
	if (!EventTag.IsValid() || !MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	// 动画编辑器预览等无 ASC 的场合跳过，否则引擎会报 Invalid ability system component
	if (!UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner)) return;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, FGameplayEventData());
}

void UAN_GameplayEventWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	SendWindowEvent(MeshComp, GetWindowBeginTag());
}

void UAN_GameplayEventWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	SendWindowEvent(MeshComp, GetWindowEndTag());
}
