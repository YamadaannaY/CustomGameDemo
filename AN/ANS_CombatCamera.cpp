#include "ANS_CombatCamera.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

bool UANS_CombatCamera::CheckRequiredTag(USkeletalMeshComponent* MeshComp) const
{
	if (!RequiredOwnerTag.IsValid())
	{
		return true;
	}

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
	return ASC && ASC->HasMatchingGameplayTag(RequiredOwnerTag);
}

void UANS_CombatCamera::PushCameraRequest(USkeletalMeshComponent* MeshComp)
{
	if (MeshComp && MeshComp->GetOwner())
	{
		if (UCombatCameraComponent* CamComp = MeshComp->GetOwner()->FindComponentByClass<UCombatCameraComponent>())
		{
			CachedRequestId = CamComp->PushRequest(CameraRequest);
		}
	}
}

void UANS_CombatCamera::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	CachedRequestId = INDEX_NONE;
	bPendingConditionCheck = false;

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	if (!CheckRequiredTag(MeshComp))
	{
		//开头帧增加一次复核操作，解决可能的时序问题
		bPendingConditionCheck = true;
		return;
	}

	PushCameraRequest(MeshComp);
}

void UANS_CombatCamera::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	// 已提交，或不在等待复核
	if (CachedRequestId != INDEX_NONE || !bPendingConditionCheck)
	{
		return;
	}

	if (!CheckRequiredTag(MeshComp))
	{
		return;
	}

	// 条件在区间内满足了：补提交
	bPendingConditionCheck = false;
	PushCameraRequest(MeshComp);
}

void UANS_CombatCamera::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	bPendingConditionCheck = false;

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
