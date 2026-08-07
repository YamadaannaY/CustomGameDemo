#include "ExtraCharacter.h"
#include "ExtraGameMovementComponent.h"
#include "WeaponSystem/ExtraGameWeaponComponent.h"
#include "WeaponSystem/ExtraGameAttributeSet.h"
#include "AbilitySystemComponent.h"


AExtraCharacter::AExtraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtraGameMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);

	// ── GAS ──
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ASC"));

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
	
	InitAbilitySystem();
}

void AExtraCharacter::InitAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		// 设置 Owner 和 Avatar
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		// 创建 AttributeSet
		if (!AttributeSet)
		{
			AttributeSet = NewObject<UExtraGameAttributeSet>(this);
			AbilitySystemComponent->AddAttributeSetSubobject<UExtraGameAttributeSet>(AttributeSet);
		}
	}
}