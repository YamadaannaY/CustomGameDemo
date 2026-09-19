#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ExtraProjectile.generated.h"

class UGameplayEffect;
class UPrimitiveComponent;
class UProjectileMovementComponent;

/** 命中判定模式：决定碰撞通道怎么配、命中后投射物是否继续飞 */
UENUM()
enum class EProjectileHitMode : uint8
{
	// 撞击即停：命中任何阻挡物都停下并销毁（箭矢）
	StopAndDamage,
	// 穿透：命中 Pawn 只结算不停止，撞到静态几何才停下销毁（剑气）
	PassThrough,
};

/**
 * 投射物基类：飞行、命中结算、生命周期。
 *
 * 派生类在构造函数里自定义碰撞体（球 / 盒）与视觉，再调 SetupProjectileCollision
 * 完成公共配置；生成时由 GA 调 InitProjectile 注入方向、速度与伤害 GE。
 *
 * 伤害由投射物自行结算（写法与 GA 基类 DoDamage 同构）
 */
UCLASS(Abstract)
class EXTRACTGAMECHARACTER_API AExtraProjectile : public AActor
{
	GENERATED_BODY()

public:
	AExtraProjectile();

	// 生成后注入飞行与伤害参数，公共函数由GA具体配置
	// InRollOffset：绕飞行轴的滚转偏移（度），只改投射物朝向、不改飞行方向；0 = 不倾斜
	void InitProjectile(AActor* InSource, TSubclassOf<UGameplayEffect> InDamageEffect, int32 InAbilityLevel,
	                    const FVector& InDir, float InSpeed, float InLifeTime, float InRollOffset = 0.f);

protected:
	//并按 HitMode 配好碰撞通道与命中回调
	void SetupProjectileCollision(UPrimitiveComponent* InCollision, UProjectileMovementComponent* InMovement);

	// 命中一个 Actor：可伤害的敌方且权威端则结算 GE；同一目标只结算一次
	void ProcessHit(AActor* HitActor);

	bool IsDamageableEnemy(AActor* Victim) const;

	void ApplyProjectileDamage(AActor* Victim) const;

	void DestroyProjectile();

	// 撞击停止：撞到墙，或非穿透模式下命中目标
	UFUNCTION()
	void HandleProjectileStopped(const FHitResult& ImpactResult);

	// 穿透模式的命中回调（Pawn 通道配的是 Overlap，不会让投射物停下）
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	                        int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// 命中判定模式，派生类构造里指定
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	EProjectileHitMode HitMode = EProjectileHitMode::StopAndDamage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UPrimitiveComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// 命中后应用的伤害 GE（由 InitProjectile 注入，留空 = 命中不造成伤害只销毁）
	UPROPERTY(EditDefaultsOnly, Category = "Projectile|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

private:
	// 伤害源，弱引用：源死亡/销毁后已射出的投射物仍安全飞行
	TWeakObjectPtr<AActor> SourceActor;

	// 穿透型已结算过的目标，避免同一目标被反复结算
	TSet<TWeakObjectPtr<AActor>> HitActors;

	int32 AbilityLevel = 1;

	// 超时销毁定时器（未命中任何对象时的兜底）
	FTimerHandle LifeTimerHandle;
};
