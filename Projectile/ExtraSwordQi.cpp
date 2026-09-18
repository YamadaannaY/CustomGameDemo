#include "ExtraSwordQi.h"

#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"

AExtraSwordQi::AExtraSwordQi()
{
	// 判定盒：X 沿飞行方向（薄），Y 是剑气的横向宽度——尺寸在蓝图子类里按特效宽度调
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SwordQiCollision"));
	CollisionBox->SetBoxExtent(FVector(25.f, 90.f, 30.f));

	UProjectileMovementComponent* Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	Movement->UpdatedComponent = CollisionBox;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 0.f;   // 直线飞行，无下坠
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;                 // InitProjectile 里覆盖

	// 穿透：命中敌人只结算，撞到静态几何才停
	HitMode = EProjectileHitMode::PassThrough;
	SetupProjectileCollision(CollisionBox, Movement);

	QiEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("QiEffect"));
	QiEffect->SetupAttachment(CollisionBox);
}
