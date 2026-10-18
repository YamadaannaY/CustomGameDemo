#include "ExtraProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/PrimitiveComponent.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GenericTeamAgentInterface.h"
#include "TimerManager.h"

AExtraProjectile::AExtraProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	// 投射物只在权威端生成（由 GA 侧判断），各端靠 Actor 复制看到它。
	bReplicates = true;
}

void AExtraProjectile::BeginPlay()
{
	Super::BeginPlay();
	
	// 移动也必须复制：否则客户端只拿到生成时的位置，投射物在客户端是静止的
	// （ProjectileMovementComponent 不复制 Velocity，模拟端不会自行往前飞）。
	SetReplicateMovement(true);
	
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

	//初始化，投射物本体属于Dynamic类
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

	//锁定目标方向/正前方
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

	//销毁定时
	if (InLifeTime > 0.f)
	{
		GetWorldTimerManager().SetTimer(LifeTimerHandle, this, &AExtraProjectile::DestroyProjectile, InLifeTime, false);
	}
}

void AExtraProjectile::HandleProjectileStopped(const FHitResult& ImpactResult)
{
	UE_LOG(LogTemp,Warning,TEXT("Stop Projectile Trigger"));
	
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

	// 挡掉重复结算
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

		UUExtraAbilitySystemStatic::SendHitReactEvent(HitActor, SourceActor.Get());

		// 能量：本投射物只结算一次，穿透剑气连穿多个目标时不重复加
		if (!bEnergyGranted && EnergyOnHit > 0.f)
		{
			bEnergyGranted = true;
			ApplySourceEnergy(EnergyOnHit);
		}
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

	//构造GE Context
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Source, Source);
	Context.AddSourceObject(Source);

	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, AbilityLevel, Context);
	if (SpecHandle.Data.Get())
	{
		// 飘字数值读结算前后的 Health/Shield 差值：护盾吸收、clamp 后的实际结果才准确
		const float HealthBefore = TargetASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute());
		const float ShieldBefore = TargetASC->GetNumericAttribute(UExtraGameAttributeSet::GetShieldAttribute());

		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);

		const float HealthAfter = TargetASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute());
		const float ShieldAfter = TargetASC->GetNumericAttribute(UExtraGameAttributeSet::GetShieldAttribute());

		// AI 血量归零会立刻回满（bRefillHealthOnZero），此时 HealthAfter 反而更大，
		// 按「打空当前血量」计算，避免显示负数或 0
		const float HealthLoss = HealthAfter > HealthBefore ? HealthBefore : HealthBefore - HealthAfter;
		const float ShieldLoss = FMath::Max(0.f, ShieldBefore - ShieldAfter);
		UUExtraAbilitySystemStatic::SendDamageNumberCue(Victim, HealthLoss + ShieldLoss);
	}
}

void AExtraProjectile::ApplySourceEnergy(float Amount) const
{
	AActor* Source = SourceActor.Get();
	if (!Source || Amount <= 0.f)
	{
		return;
	}

	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Source);
	if (!ASI)
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = ASI->GetAbilitySystemComponent();
	if (!SourceASC)
	{
		return;
	}

	const FGameplayAttribute EnergyAttribute = UExtraGameAttributeSet::GetEnergyValueAttribute();
	SourceASC->SetNumericAttributeBase(EnergyAttribute, SourceASC->GetNumericAttribute(EnergyAttribute) + Amount);
}

void AExtraProjectile::DestroyProjectile()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifeTimerHandle);
	}
	Destroy();
}