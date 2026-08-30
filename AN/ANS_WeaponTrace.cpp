#include "ANS_WeaponTrace.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"

void UANS_WeaponTrace::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	if (UExtraGameWeaponComponent* WeaponComp = MeshComp->GetOwner()->FindComponentByClass<UExtraGameWeaponComponent>())
	{
		WeaponComp->BeginWeaponTrace();
	}
}

void UANS_WeaponTrace::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	if (UExtraGameWeaponComponent* WeaponComp = MeshComp->GetOwner()->FindComponentByClass<UExtraGameWeaponComponent>())
	{
		WeaponComp->TickWeaponTrace();
	}
}

void UANS_WeaponTrace::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	if (UExtraGameWeaponComponent* WeaponComp = MeshComp->GetOwner()->FindComponentByClass<UExtraGameWeaponComponent>())
	{
		WeaponComp->EndWeaponTrace();
	}
}

FString UANS_WeaponTrace::GetNotifyName_Implementation() const
{
	return TEXT("WeaponTrace");
}
