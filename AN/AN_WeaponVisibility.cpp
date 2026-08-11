#include "AN_WeaponVisibility.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"

void UAN_WeaponVisibility::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	const AExtraCharacter* Character = Cast<AExtraCharacter>(MeshComp->GetOwner());
	if (!Character)
	{
		return;
	}

	UExtraGameWeaponComponent* WeaponComp = Character->GetWeaponComponent();
	if (!WeaponComp)
	{
		return;
	}

	switch (Target)
	{
	case EWeaponVisibilityTarget::SingleWeapon:
		if (!WeaponTag.IsValid())
		{
			return;
		}
		if (bShow)
		{
			WeaponComp->ShowWeaponEntry(WeaponTag);
		}
		else
		{
			WeaponComp->HideWeaponEntry(WeaponTag);
		}
		break;

	case EWeaponVisibilityTarget::CurrentGroup:
		if (bShow)
		{
			WeaponComp->ShowAllWeapons();
		}
		else
		{
			WeaponComp->HideWeapon();
		}
		break;
	}
}

FString UAN_WeaponVisibility::GetNotifyName_Implementation() const
{
	const FString Action = bShow ? TEXT("Show") : TEXT("Hide");
	switch (Target)
	{
	case EWeaponVisibilityTarget::SingleWeapon:
		if (WeaponTag.IsValid())
		{
			return FString::Printf(TEXT("%s %s"), *Action, *WeaponTag.GetTagName().ToString());
		}
		return FString::Printf(TEXT("%s Weapon"), *Action);
	case EWeaponVisibilityTarget::CurrentGroup:
		return FString::Printf(TEXT("%s All Weapons"), *Action);
	default:
		return FString::Printf(TEXT("%s Weapon"), *Action);
	}
}
