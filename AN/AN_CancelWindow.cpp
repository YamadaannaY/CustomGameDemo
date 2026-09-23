#include "AN_CancelWindow.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

namespace
{
	// 开/关窗事件只做通知；由 UExtraGameplayAbility 的窗口持有逻辑监听
	void SendCancelWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag)
	{
		if (!MeshComp)
		{
			return;
		}

		AActor* Owner = MeshComp->GetOwner();
		if (!Owner) return;

		// 动画编辑器预览等无 ASC 的场合跳过，否则引擎会报 Invalid ability system component
		if (!UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner)) return;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, FGameplayEventData());
	}
}

void UAN_CancelWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 进窗：持有者据此撤销自身封锁并登记为「可被任何 GA 取消」
	SendCancelWindowEvent(MeshComp, UUExtraAbilitySystemStatic::GetCancelWindowBeginTag());
}

void UAN_CancelWindow::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	// 仅在窗口内有移动输入时才打断；离开窗口（NotifyEnd）后不再检测。
	const AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(Owner);
	if (!PlayerChar || !PlayerChar->HasMoveInput())
	{
		return;
	}

	UAbilitySystemComponent* OwnerASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	if (!OwnerASC)
	{
		return;
	}

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, UUExtraAbilitySystemStatic::GetAbilityCancelTag(), FGameplayEventData());
}

void UAN_CancelWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	// 出窗：持有者恢复自身封锁、解除登记
	SendCancelWindowEvent(MeshComp, UUExtraAbilitySystemStatic::GetCancelWindowEndTag());
}

FString UAN_CancelWindow::GetNotifyName_Implementation() const
{
	return TEXT("CancelWindow");
}
