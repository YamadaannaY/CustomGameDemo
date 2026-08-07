#include "ExtraCharacter.h"
#include "ExtraGameMovementComponent.h"
#include "WeaponSystem/ExtraGameWeaponComponent.h"
#include "GAS/ExtraAbilitySystemComponent.h"


AExtraCharacter::AExtraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtraGameMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);

	// ── GAS ──
	AbilitySystemComponent = CreateDefaultSubobject<UExtraAbilitySystemComponent>(TEXT("ASC"));

	// ── 武器组件 ──
	WeaponComponent = CreateDefaultSubobject<UExtraGameWeaponComponent>(TEXT("WeaponComponent"));
}

UAbilitySystemComponent* AExtraCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AExtraCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void AExtraCharacter::ServerSideInit()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		AbilitySystemComponent->ServerSideInit();
	}

	// ASC 初始化完毕后，通知武器组件装备默认武器组
	if (WeaponComponent)
	{
		WeaponComponent->OnASCInitialized();
	}
}
