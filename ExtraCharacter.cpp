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

	// 把目标跳跃高度换算成起跳初速度：v = sqrt(2 * g * h)（g 取绝对值，单位均为 cm）
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const float Gravity = FMath::Abs(Movement->GetGravityZ());
		Movement->JumpZVelocity = FMath::Sqrt(2.f * Gravity * JumpHeight);
	}
}

void AExtraCharacter::ServerSideInit()
{
	if (bGASInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ExtraCharacter] ServerSideInit: GAS already initialized. Skipping duplicate call."));
		return;
	}

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

	bGASInitialized = true;
}
