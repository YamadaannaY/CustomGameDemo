#pragma once

#include "CoreMinimal.h"
#include "ExtraPlayerCharacter.h"
#include "AN/AN_FootPlant.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "ExtraGameAnimInstance.generated.h"

class UExtraGameMovementComponent;
class AExtraPlayerCharacter;

/**
 * Idle Action 配置条目：一个待机动作 = 动画序列 + 播放持续时间
 */
USTRUCT(BlueprintType)
struct EXTRACTGAMECHARACTER_API FIdleActionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction")
	TObjectPtr<UAnimSequence> AnimSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction",
		meta = (ClampMin = "0.1"))
	float Duration = 10.0f;
};

/**
 * Custom AnimInst
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void OnFootPlantNotify(EFootPlant Foot);
	
	
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool HasAcceleration() const {return Acceleration > 0.f;}

	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsJumping() const {return  bisJumping;}

	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsFalling() const {return  bisFalling;}

	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsWalking() const {return  bisWalking;}
	
	/*
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE  bool IsWalkingMode() const {return bWalkMode;}
	*/

	// 玩家已松开移动键，锁速度等待下一个 FootPlant Notify 退出
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	bool bRequestStop = false;

	// FootPlant Notify 触发且 bRequestStop==true 时设为 true，作为AnimBP Transition Rule进入停步状态机
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	bool bCanEnterStop = false;

	// 记录应该在哪个脚落地时停步
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	EFootPlant PendingStopFoot = EFootPlant::None;

	// 请求停步（由 Character 在松开移动键时调用），开始锁速度并放开进入停步权限
	void RequestStop();

	// 清除停步请求，重置时使用（新输入 / 跳跃 / 进入 Stop 状态后）
	void ClearStopRequest();

	// AnimBP 进入Stop状态时调用，消费停步请求
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	void OnStopStateEntered();
	
	//进入左停步的过渡条件
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE bool CanEnterLeftStop() const { return bCanEnterStop && PendingStopFoot == EFootPlant::Left; }

	//进入右停步的过渡条件
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE bool CanEnterRightStop() const { return bCanEnterStop && PendingStopFoot == EFootPlant::Right; }

	// BlendSpace 驱动参数 
	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float GroundSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float Stride = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float WalkRunBlend = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float MoveDirection = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	bool bIsMoving = false;
	
	// AN_EvadeToSprint 截断 Montage 后置为 true
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Sprint")
	bool bEvadeToSprint = false;

	// 角色当前处于 Sprint 状态（AnimBP 进入 Sprint 后置 true，退出后置 false）
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Sprint")
	bool bIsSprinting = false;

	// Sprint 模式下的速度，AnimBP 用来驱动 Sprint BlendSpace
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Sprint")
	float SprintSpeed = 0.f;

	// AnimBP 退出 Sprint 状态时调用
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	void OnSprintStateLeft();

	// ── 空中攻击标记 ────────────────────────────────────────
	// GA_AirAttack 激活期间为 true，AnimBP 的 falling 过渡条件用它来「让位」，
	// 避免 Start→Loop 切换的权重真空帧被状态机 falling 抢占导致瞬变。
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|AirAttack")
	bool bAirAttacking = false;

	FVector CacheVelocity = FVector::ZeroVector;

	// ── Idle Action 系统 ─────────────────────────────────────────
	// 配置：一个条目对应一个待机动作变体（动画 + 持续时间）
	// 数组长度 N = 动作数量，AnimBP 中需要 N 个对应的 Action 子状态
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction|Config")
	TArray<FIdleActionEntry> IdleActionEntries;

	// Idle 姿态下等待多少秒后触发随机动作
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction|Config",
		meta = (ClampMin = "0.5"))
	float IdleChangeInterval = 5.0f;

	// 0 = Idle 姿态；1..N = 正在播放对应 IdleActionEntries[Index-1] 的动作
	// AnimBP Transition Rule 读取此值来切换状态
	UPROPERTY(BlueprintReadOnly, Category = "IdleAction|Runtime")
	int32 IdleActionIndex = 0;

	// 当前活跃动作的动画序列，在进入 Action 阶段时设置
	// 允许 AnimBP 使用单个 Action 状态动态绑定序列（备选方案）
	UPROPERTY(BlueprintReadOnly, Category = "IdleAction|Runtime")
	TObjectPtr<UAnimSequence> CurrentIdleActionSequence;
	// ─────────────────────────────────────────────────────────────

private:
	UPROPERTY(EditDefaultsOnly,Category = "StrideSetting")
	float StrideCoefficient=1.f;
	
	float Acceleration =0 ;

	bool bWalkMode = false;

	UPROPERTY()
	AExtraPlayerCharacter* OwnerCharacter;

	UPROPERTY()
	UExtraGameMovementComponent* OwnerMovementComp;

	EMovementMode MovementMode;

	bool bisFalling;
	bool bisJumping;
	bool bisWalking;

	// ── Idle Action 系统（私有） ─────────────────────────────────
	// 统一计时器：Idle 和 Action 阶段共用，阶段切换时归零
	float IdlePhaseTimer = 0.f;

	// 上一轮选中动作的 0-based 索引，初始 -1 表示"无上一轮"
	// 跨移动周期保留，防止停下来后立即重复同一动作
	int32 PrevIdleActionIndex = -1;

	// 核心更新，由NativeUpdateAnimation在Idle状态下每帧调用,
	void UpdateIdleActionSystem(float DeltaSeconds);

	// O(1) 无重复随机选取下一个动作，返回 1-based 索引（0 表示无可用动作）
	int32 PickNextIdleAction();
	// ─────────────────────────────────────────────────────────────
};
