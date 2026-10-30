#include "GameplayCueNotify_DamageNumber.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "ExtractGameCharacter/UI/DamageNumberActor.h"

bool UGameplayCueNotify_DamageNumber::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	if (!MyTarget || !DamageNumberActorClass)
	{
		return false;
	}

	UWorld* World = MyTarget->GetWorld();
	
	//Server无需飘字
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	FVector SpawnLocation = Parameters.Location;
	if (const USkeletalMeshComponent* Mesh = MyTarget->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (AttachBoneName != NAME_None && Mesh->DoesSocketExist(AttachBoneName))
		{
			SpawnLocation = Mesh->GetSocketLocation(AttachBoneName);
		}
	}
	if (SpawnLocation.IsNearlyZero())
	{
		SpawnLocation = MyTarget->GetActorLocation();
	}
	SpawnLocation.Z += SpawnHeightOffset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ADamageNumberActor* DamageNumber = World->SpawnActor<ADamageNumberActor>(
		DamageNumberActorClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams))
	{
		DamageNumber->InitDamageNumber(Parameters.RawMagnitude);
	}

	return true;
}
