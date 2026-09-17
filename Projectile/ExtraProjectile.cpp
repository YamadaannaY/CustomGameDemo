#include "ExtraProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/PrimitiveComponent.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GenericTeamAgentInterface.h"
#include "TimerManager.h"

AExtraProjectile::AExtraProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AExtraProjectile::SetupProjectileCollision(UPrimitiveComponent* InCollision, UProjectileMovementComponent* InMovement)
{
	if (!InCollision || !InMovement)
	{
		return;
	}

	CollisionComponent = InCollision;
	ProjectileMovement = InMovement;
	SetRootComponent(InCollision);

	InCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InCollision->SetCollisionObjectType(ECC_WorldDynamic);
	InCollision->SetCollisionResponseToAllChannels(ECR_Ignore);

	// 两种模式的差别就在这里：
	//   停止型——全部阻挡，靠「撞击停止」收尾；
	//   穿透型——Pawn 走重叠（命中只结算、继续飞），静态几何仍阻挡（撞墙才停）。
	// 两个 Actor 的碰撞响应取「更松」的一方，所以这里把 Pawn 设成 Overlap 即可保证不会撞停。
	const ECollisionResponse PawnResponse =
		(HitMode == EProjectileHitMode::PassThrough) ? ECR_Overlap : ECR_Block;

	InCollision->SetCollisionResponseToChannel(ECC_Pawn, PawnResponse);
	InCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	InCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

	InMovement->OnProjectileStop.AddDynamic(this, &AExtraProjectile::HandleProjectileStopped);

	if (HitMode == EProjectileHitMode::PassThrough)
	{
		InCollision->SetGenerateOverlapEvents(true);
		InCollision->OnComponentBeginOverlap.AddDynamic(this, &AExtraProjectile::HandleBeginOverlap);
	}
}

void AExtraProjectile::InitProjectile(AActor* InSource, TSubclassOf<UGameplayEffect> InDamageEffect, int32 InAbilityLevel,
                                      const FVector& InDir, float InSpeed, float InLifeTime)
{
	SourceActor = InSource;
	DamageEffectClass = InDamageEffect;
	AbilityLevel = InAbilityLevel;
	HitActors.Reset();

	// 忽略来源自身（角色胶囊等），避免出膛瞬间与自身撞停
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
		GetWorldTimerManager().SetTimer(LifeTimerHandle, this, &AExtraProjectile::DestroyProjectile, InLifeTime, false);
	}
}

void AExtraProjectile::HandleProjectileStopped(const FHitResult& ImpactResult)
{
	if (AActor* HitActor = ImpactResult.GetActor())
	{
		ProcessHit(HitActor);
	}

	// 撞到不可穿透的物体（墙 / 地面）：不论哪种模式，投射物到此为止
	DestroyProjectile();
}

void AExtraProjectile::HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                          UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                          const FHitResult& SweepResult)
{
	// 穿透：只结算，不销毁，投射物继续飞
	ProcessHit(OtherActor);
}

void AExtraProjectile::ProcessHit(AActor* HitActor)
{
	if (!HitActor || HitActor == SourceActor.Get())
	{
		return;
	}

	// 穿透型会与同一目标反复重叠，这里挡掉重复结算
	if (HitActors.Contains(HitActor))
	{
		return;
	}
	HitActors.Add(HitActor);

	if (HasAuthority() && IsDamageableEnemy(HitActor))
	{
		ApplyProjectileDamage(HitActor);
	}
}

bool AExtraProjectile::IsDamageableEnemy(AActor* Victim) const
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

void AExtraProjectile::ApplyProjectileDamage(AActor* Victim) const
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

	// 与 GA 基类 DoDamage 同构：伤害来源归属为发起攻击的角色
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

void AExtraProjectile::DestroyProjectile()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifeTimerHandle);
	}
	Destroy();
}
