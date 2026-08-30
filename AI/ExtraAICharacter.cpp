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

	// 木桩不装备武器：跳过基类里 WeaponComponent->OnASCInitialized()
	//（无 WeaponDataAsset 时基类实现会刷报错）
	bGASInitialized = true;
}

void AExtraAICharacter::BeginPlay()
{
	// 无 Controller Possess，主动初始化 GAS。
	// 必须在 Super::BeginPlay() 之前：基类在 BeginPlay 里配置头顶血条
	//（ConfigureOverHeadStatusWidget 需 ASC 已注册属性才能读到初始值）。
	if (HasAuthority())
	{
		ServerSideInit();
	}

	Super::BeginPlay();
}
