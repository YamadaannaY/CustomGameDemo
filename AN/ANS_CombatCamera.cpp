#include "ANS_CombatCamera.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

void UANS_CombatCamera::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	CachedRequestId = INDEX_NONE;

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	// 条件提交：填了 RequiredOwnerTag 时，Owner 的 ASC 没有该 Tag 就整段跳过
	// （CachedRequestId 保持 INDEX_NONE，NotifyEnd 也不会 Pop）
	if (RequiredOwnerTag.IsValid())
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
		if (!ASC || !ASC->HasMatchingGameplayTag(RequiredOwnerTag))
		{
			return;
		}
	}

	if (UCombatCameraComponent* CamComp = MeshComp->GetOwner()->FindComponentByClass<UCombatCameraComponent>())
	{
		CachedRequestId = CamComp->PushRequest(CameraRequest);
	}
}

void UANS_CombatCamera::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (CachedRequestId == INDEX_NONE)
	{
		return;
	}

	if (MeshComp && MeshComp->GetOwner())
	{
		if (UCombatCameraComponent* CamComp = MeshComp->GetOwner()->FindComponentByClass<UCombatCameraComponent>())
		{
			CamComp->PopRequest(CachedRequestId);
		}
	}

	CachedRequestId = INDEX_NONE;
}

FString UANS_CombatCamera::GetNotifyName_Implementation() const
{
	return TEXT("CombatCamera");
}
