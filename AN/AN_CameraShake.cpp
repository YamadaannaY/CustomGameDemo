#include "AN_CameraShake.h"
#include "Camera/CameraShakeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	bool IsDedicatedServer(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World && World->GetNetMode() == NM_DedicatedServer;
	}
}

void UAN_CameraShake::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner() || ShakeClass.IsNull() || IsDedicatedServer(MeshComp))
	{
		return;
	}

	// 判定通过后再加载
	TSubclassOf<UCameraShakeBase> ShakeAsset = ShakeClass.LoadSynchronous();
	if (!ShakeAsset)
	{
		return;
	}

	const FVector Epicenter = MeshComp->GetComponentLocation();

	if (!bForSelf)
	{
		//遍历所有PlayerController并施加ClientCameraShake
		UGameplayStatics::PlayWorldCameraShake(MeshComp, ShakeAsset, Epicenter, InnerRadius, OuterRadius, Falloff, bOrientTowardsEpicenter);
		return;
	}

	// 只震该角色对应的本地玩家；AI 控制或无 PC 时不播
	AActor* Owner = MeshComp->GetOwner();
	APlayerController* PC = nullptr;
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		PC = Cast<APlayerController>(Pawn->GetController());
	}
	if (!PC)
	{
		PC = Cast<APlayerController>(Owner->GetInstigatorController());
	}

	if (PC && PC->IsLocalController())
	{
		PC->ClientStartCameraShake(ShakeAsset, Scale);
	}
}

FString UAN_CameraShake::GetNotifyName_Implementation() const
{
	return ShakeClass.IsNull() ? TEXT("Came·a Shake") : ShakeClass.ToSoftObjectPath().GetAssetName();
}
