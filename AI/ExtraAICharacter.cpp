#include "ExtraAICharacter.h"
#include "Components/CapsuleComponent.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"

AExtraAICharacter::AExtraAICharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 玩家默认胶囊对自定义 trace 通道(GameTraceChannel1)为 Ignore，
	// 木桩需显式 Block，武器轨迹扫描(SweepMultiByChannel)才会命中本 Actor。
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToChannel(WeaponTraceChannel.GetValue(), ECR_Block);
	}
}

void AExtraAICharacter::ServerSideInit()
{
	if (bGASInitialized)
	{
		return;
	}

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		AbilitySystemComponent->ServerSideInit();
	}
	
	bGASInitialized = true;
}

void AExtraAICharacter::BeginPlay()
{
	if (HasAuthority())
	{
		ServerSideInit();
	}

	Super::BeginPlay();
}
