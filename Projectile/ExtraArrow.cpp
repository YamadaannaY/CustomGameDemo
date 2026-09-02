#include "ExtraArrow.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GenericTeamAgentInterface.h"
#include "TimerManager.h"

AExtraArrow::AExtraArrow()
{
	PrimaryActorTick.bCanEverTick = false;

	// 碰撞球体负责「飞行 + 命中」：投射物按它做扫描，碰撞停止时通过 OnProjectileStop 回调
	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("ArrowCollision"));
	RootComponent = CollisionComponent;
	CollisionComponent->InitSphereRadius(6.f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

	// 箭身纯视觉，不参与碰撞（避免 StaticMesh 复杂碰撞干扰命中判定）
	ArrowMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMeshComponent->SetupAttachment(RootComponent);
	ArrowMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.f;   // 直线飞行，无下坠
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 0.f;                 // InitShot 里按实际速度覆盖
	ProjectileMovement->OnProjectileStop.AddDynamic(this, &AExtraArrow::OnArrowStopped);
}

void AExtraArrow::InitShot(AActor* InSource, TSubclassOf<UGameplayEffect> InDamageEffect, int32 InAbilityLevel,
                           const FVector& InDir, float InSpeed, float InLifeTime)
{
	SourceActor = InSource;
	DamageEffectClass = InDamageEffect;
	AbilityLevel = InAbilityLevel;

	// 忽略来源自身（角色胶囊 / 弓等），避免出膛瞬间与自身碰撞停止
	if (CollisionComponent && InSource)
	{
		CollisionComponent->IgnoreActorWhenMoving(InSource, true);
	}

	const FVector SafeDir = InDir.GetSafeNormal();
	if (ProjectileMovement && InSpeed > 0.f)
	{
		ProjectileMovement->InitialSpeed = InSpeed;
		ProjectileMovement->MaxSpeed = InSpeed;
		ProjectileMovement->Velocity = SafeDir * InSpeed;
	}
	if (!SafeDir.IsNearlyZero())
	{
		SetActorRotation(SafeDir.Rotation());
	}

	if (InLifeTime > 0.f)
	{
		GetWorldTimerManager().SetTimer(LifeTimerHandle, this, &AExtraArrow::DestroyArrow, InLifeTime, false);
	}
}

void AExtraArrow::SetArrowMesh(UStaticMesh* InMesh)
{
	if (ArrowMeshComponent && InMesh)
	{
		ArrowMeshComponent->SetStaticMesh(InMesh);
	}
}

void AExtraArrow::OnArrowStopped(const FHitResult& ImpactResult)
{
	AActor* HitActor = ImpactResult.GetActor();
	if (!HitActor)
	{
		return;
	}

	OnImpact(HitActor);
}

void AExtraArrow::OnImpact(AActor* HitActor)
{
	// 可伤害的敌方（Team 敌对 + 存活）且权威端 → 结算 GE
	if (HitActor && HasAuthority() && IsDamageableEnemy(HitActor))
	{
		ApplyArrowDamage(HitActor);
	}

	// 命中世界物体 / 无效对象，或伤害结算完成，一律销毁
	DestroyArrow();
}

bool AExtraArrow::IsDamageableEnemy(AActor* Victim) const
{
	const AExtraCharacter* Source = Cast<AExtraCharacter>(SourceActor.Get());
	if (!Source || !Victim || Victim == Source)
	{
		return false;
	}

	const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Victim);
	if (!TeamAgent || TeamAgent->GetGenericTeamId() == Source->GetGenericTeamId())
	{
		return false;
	}

	const UAbilitySystemComponent* VictimASC = nullptr;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Victim))
	{
		VictimASC = ASI->GetAbilitySystemComponent();
	}
	if (!VictimASC)
	{
		return false;
	}

	return VictimASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute()) > 0.f;
}

void AExtraArrow::ApplyArrowDamage(AActor* Victim) const
{
	AActor* Source = SourceActor.Get();
	if (!Source || !Victim || !DamageEffectClass)
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = nullptr;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Source))
	{
		SourceASC = ASI->GetAbilitySystemComponent();
	}
	UAbilitySystemComponent* TargetASC = nullptr;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Victim))
	{
		TargetASC = ASI->GetAbilitySystemComponent();
	}
	if (!SourceASC || !TargetASC)
	{
		return;
	}

	// 与 GA 基类 DoDamage 同构：伤害来源归属为发起射击的角色
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Source, Source);
	Context.AddSourceObject(Source);

	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, AbilityLevel, Context);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.Get())
	{
		return;
	}
	SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
}

void AExtraArrow::DestroyArrow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifeTimerHandle);
	}
	Destroy();
}
