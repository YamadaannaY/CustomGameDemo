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
	
	const ECollisionResponse PawnResponse =
		(HitMode == EProjectileHitMode::PassThrough) ? ECR_Overlap : ECR_Block;

	InCollision->SetCollisionResponseToChannel(ECC_Pawn, PawnResponse);
	
	//对世界物体响应Block进行销毁
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
                                      const FVector& InDir, float InSpeed, float InLifeTime, float InRollOffset)
{
	SourceActor = InSource;
	DamageEffectClass = InDamageEffect;
	AbilityLevel = InAbilityLevel;
	HitActors.Reset();

	// 忽略来源自身
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
	// 朝向飞行方向；Roll 偏移只让投射物绕飞行轴滚转，不影响飞出去的方向
	if (!SafeDir.IsNearlyZero())
	{
		FRotator FireRot = SafeDir.Rotation();
		FireRot.Roll += InRollOffset;
		SetActorRotation(FireRot);
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

	const bool bHasAuthorityNow = HasAuthority();
	const bool bIsEnemy = IsDamageableEnemy(HitActor);
	
	if (bHasAuthorityNow && bIsEnemy)
	{
		ApplyProjectileDamage(HitActor);

		// 命中确认才加能量（从目标身上擦过去不算）
		ApplySourceEnergyGain();
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

void AExtraProjectile::ApplySourceEnergyGain() const
{
	if (SourceEnergyPerHit <= 0.f)
	{
		return;
	}

	AActor* Source = SourceActor.Get();
	if (!Source)
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = nullptr;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Source))
	{
		SourceASC = ASI->GetAbilitySystemComponent();
	}
	if (!SourceASC)
	{
		return;
	}
	 
	const FGameplayAttribute EnergyAttribute = UExtraGameAttributeSet::GetEnergyValueAttribute();
	SourceASC->SetNumericAttributeBase(EnergyAttribute, SourceASC->GetNumericAttribute(EnergyAttribute) + SourceEnergyPerHit);
}

void AExtraProjectile::DestroyProjectile()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifeTimerHandle);
	}
	Destroy();
}
