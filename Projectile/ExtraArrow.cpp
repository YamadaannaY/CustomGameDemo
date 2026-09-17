#include "ExtraArrow.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"

AExtraArrow::AExtraArrow()
{
	// 碰撞球负责「飞行 + 命中」：投射物按它做扫描，撞停时由基类回调收尾
	USphereComponent* Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("ArrowCollision"));
	Sphere->InitSphereRadius(6.f);

	UProjectileMovementComponent* Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	Movement->UpdatedComponent = Sphere;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 0.f;   // 直线飞行，无下坠
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;                 // InitProjectile 里按实际速度覆盖

	// 箭矢命中即停、随即销毁
	HitMode = EProjectileHitMode::StopAndDamage;
	SetupProjectileCollision(Sphere, Movement);

	// 箭身纯视觉，不参与碰撞（避免 StaticMesh 复杂碰撞干扰命中判定）
	ArrowMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMeshComponent->SetupAttachment(Sphere);
	ArrowMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AExtraArrow::SetArrowMesh(UStaticMesh* InMesh)
{
	if (ArrowMeshComponent && InMesh)
	{
		ArrowMeshComponent->SetStaticMesh(InMesh);
	}
}
