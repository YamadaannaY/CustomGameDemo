#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ExtraArrow.generated.h"

class UGameplayEffect;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 箭矢投射物（重击弓射使用）
 *
 * 由 GA 生成后 InitShot 注入方向/初速/伤害源与 GE：
 * - 直线无重力飞行（目标在 GA 激活期间被时停，不需要追踪）
 * - 命中可伤害的敌方（权威端）→ 应用注入的 GE 后销毁
 * - 命中世界/死亡对象等无效目标 → 直接销毁
 * - 无目标空射 → 超时销毁
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraArrow : public AActor
{
	GENERATED_BODY()

public:
	AExtraArrow();

	// 生成后注入飞行与伤害参数（BeginPlay 已执行，故在此统一初始化运动）
	void InitShot(AActor* InSource, TSubclassOf<UGameplayEffect> InDamageEffect, int32 InAbilityLevel,
	              const FVector& InDir, float InSpeed, float InLifeTime);

	// 若需由 GA 侧指定箭身 mesh（未指定时使用蓝图配置的 mesh）
	void SetArrowMesh(UStaticMesh* InMesh);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	USphereComponent* CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	UStaticMeshComponent* ArrowMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	UProjectileMovementComponent* ProjectileMovement;

	// 命中敌方后应用的伤害 GE（由 InitShot 注入，可留空 = 命中不造成伤害只销毁）
	UPROPERTY(EditDefaultsOnly, Category = "Arrow|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

private:
	// 投射物因碰撞停止时回调（携带命中结果）
	UFUNCTION()
	void OnArrowStopped(const FHitResult& ImpactResult);

	// 命中目标处理：可伤害敌方则结算 GE，随后一律销毁
	void OnImpact(AActor* HitActor);

	bool IsDamageableEnemy(AActor* Victim) const;

	void ApplyArrowDamage(AActor* Victim) const ;

	void DestroyArrow();

	// 伤害源（发起射击的角色），弱引用：源死亡/销毁后射出的箭仍安全飞行
	TWeakObjectPtr<AActor> SourceActor;

	int32 AbilityLevel = 1;

	// 超时销毁定时器（未命中任何对象时的兜底）
	FTimerHandle LifeTimerHandle;
};
