#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ExtraGameplayAbility.h"
#include "GA_HeavyAttack.generated.h"

class AActor;
class AExtraArrow;
class UAnimMontage;
class UGameplayEffect;
class UStaticMesh;

/**
 * 重击 GA（弓射）：
 * 激活时冻结全场敌方（时停），播放三段射击 Montage；
 * Montage 内各放箭帧的 AN 发送 ability.heavyattack.shoot，每次收到即从弓出箭 Socket 生成一支
 * 直线飞向「激活时锁定的目标位置」的箭矢（目标被击杀后不换目标，继续射向该位置；无锁定则朝正前）。
 * 箭矢伤害由 AExtraArrow 自行结算，箭矢 GE 在此配置。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_HeavyAttack : public UExtraGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_HeavyAttack();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Montage")
	UAnimMontage* HeavyAttackMontage;

	// ── 弓射箭配置 ─────────────────────────────────────────────
	// 弓在武器组中对应的 WeaponTag（经武器组件 GetWeaponMeshByTag 找到弓的 StaticMesh）
	UPROPERTY(EditDefaultsOnly, Category = "Arrow")
	FGameplayTag BowWeaponTag;

	// 弓 StaticMesh 上出箭的 Socket 名
	UPROPERTY(EditDefaultsOnly, Category = "Arrow")
	FName ArrowSpawnSocketName;

	// 生成的箭矢 Actor 类
	UPROPERTY(EditDefaultsOnly, Category = "Arrow")
	TSubclassOf<AExtraArrow> ArrowActorClass;

	// 可选：由 GA 直接指定箭身 StaticMesh（留空则用 ArrowActorClass 默认/蓝图里配的 mesh）
	UPROPERTY(EditDefaultsOnly, Category = "Arrow")
	TSoftObjectPtr<UStaticMesh> ArrowStaticMesh;

	// 出膛速度（cm/s）
	UPROPERTY(EditDefaultsOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float ArrowSpeed = 2500.f;

	// 未命中任何对象的超时销毁时间（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float ArrowLifeTime = 3.f;

	// 命中敌方后复用的箭矢 GE
	UPROPERTY(EditDefaultsOnly, Category = "Arrow")
	TSubclassOf<UGameplayEffect> ArrowDamageEffect;

	// ── 时停（GA 激活期间冻结全场敌方）─────────────────────────
	struct FHeavyTimeFreezeEntry
	{
		TWeakObjectPtr<AActor> Enemy;
		float OriginalTimeDilation = 1.f;
	};
	TArray<FHeavyTimeFreezeEntry> FrozenEnemies;

	// ── 瞄准快照：激活时锁定目标的位置，整个 GA 不再变更 ────────
	// （目标被击杀后 LockOn 会自动清锁/重选，故不能每箭重读 GetLockTarget）
	bool bHasAimPoint = false;
	FVector AimTargetLocation = FVector::ZeroVector;

	void SetupShootListener();

	// Montage 放箭帧事件回调（每收到一次射一箭）
	UFUNCTION()
	void HandleShootRequest(FGameplayEventData EventData);

	// 冻结全场存活敌方（per-actor CustomTimeDilation=0），并记录原值供解冻恢复
	void ApplyTimeFreeze();

	// 恢复各敌方的原始时间膨胀（幂等）
	void ReleaseTimeFreeze();

	// 激活时缓存锁定目标位置为瞄准快照
	void UpdateAimSnapshot();

	// 从弓出箭 Socket 生成一支射向快照点的箭
	void SpawnArrowAtSocket();
};
