#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ULockOnComponent.generated.h"

class AActor;

/**
 * 锁定组件：周期检测 Owner 周围TeamID敌对 Pawn，锁定距离最近的目标。
 *
 * 锁定结果供攻击 GA（基类 bRotateToLockTarget）读取，通过 MotionWarping
 * 使攻击动画朝向锁定目标释放。
 * 
 * 纯本地组件，不参与网络复制：只在本地控制的 Pawn 上执行检测。
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API ULockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULockOnComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 当前锁定目标（可为空，获取弱指针对象）
	UFUNCTION(BlueprintPure, Category = "LockOn")
	AActor* GetLockTarget() const { return CurrentLockTarget.Get(); }

	// 当前是否持有有效锁定目标
	UFUNCTION(BlueprintPure, Category = "LockOn")
	bool HasLockTarget() const { return CurrentLockTarget.IsValid(); }

	// 手动调用解除锁定
	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void ClearLockTarget();

	// 立即重新扫描一次（不等检测周期，ForceRefresh 后仍按周期继续）
	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void ForceRefresh();

protected:
	// 检测半径（cm）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (ClampMin = "0.0"))
	float LockRadius = 1000.f;

	// 解除锁定距离（cm）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (ClampMin = "0.0"))
	float LockBreakRange = 1500.f;

	// 检测周期（秒）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (ClampMin = "0.05"))
	float DetectInterval = 0.2f;

	// 是否要求视线无遮挡（被墙体挡住的目标不锁定）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bRequireLOS = true;

	// 已有锁定目标时是否保持：true=仅在目标失效/超距时才重扫；false=每周期重选最近
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bHoldTargetUntilBreak = true;

	// 周期检测回调
	void UpdateLockTarget();

	// 候选是否可作为锁定目标（敌对 + 存活 + 可选 LOS）
	bool IsValidTarget(AActor* Candidate) const;

	// 敌对判断：TeamID 与 Owner 不同
	bool IsEnemy(AActor* Candidate) const;

	// 存活判断：ASC Health > 0
	bool IsAlive(AActor* Candidate) const;

	// 视线判断：Owner→Candidate 之间无遮挡
	bool HasLineOfSight(AActor* Candidate) const;

	FTimerHandle DetectTimerHandle;

	// 当前锁定目标（弱引用，目标销毁后自动失效）
	TWeakObjectPtr<AActor> CurrentLockTarget;
};
