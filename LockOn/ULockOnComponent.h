#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ULockOnComponent.generated.h"

class AActor;

/**
 * 锁定组件：周期检测 Owner 周围 TeamID 敌对 Pawn，锁定距离最近的目标。
 *
 * 检测与校验都在服务端执行，结果经 CurrentLockTarget 属性复制下发各端。
 *
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API ULockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULockOnComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 当前锁定目标
	AActor* GetLockTarget() const { return CurrentLockTarget; }

	// 当前是否持有有效锁定目标
	bool HasLockTarget() const { return IsValid(CurrentLockTarget.Get()); }

	// 强制解除锁定
	void ClearLockTarget();

	// 立即重新扫描一次
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

	// 是否要求视线无遮挡
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bRequireLOS = true;

	// 已有锁定目标时是否保持：true=仅在目标失效/超距时才重扫；false=每周期重选最近
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bHoldTargetUntilBreak = true;

	// 服务端周期检测回调
	void UpdateLockTarget();

	// 复制到达（各端）：本端表现/日志用
	UFUNCTION()
	void OnRep_CurrentLockTarget();

	// 候选是否可作为锁定目标（敌对 + 存活 + 可选 LOS）
	bool IsValidTarget(AActor* Candidate) const;

	// 敌对判断
	bool IsEnemy(AActor* Candidate) const;

	// 存活判断
	bool IsAlive(AActor* Candidate) const;

	// 视线判断：Owner→Candidate 之间无遮挡
	bool HasLineOfSight(AActor* Candidate) const;

	// 打印锁定目标日志
	void LogLockTargetChanged() const;

	FTimerHandle DetectTimerHandle;

	// 服务端权威锁定目标，复制到所有本地+模拟客户端
	UPROPERTY(ReplicatedUsing = OnRep_CurrentLockTarget)
	TObjectPtr<AActor> CurrentLockTarget;
};
