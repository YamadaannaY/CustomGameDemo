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
	//记录停步之后第一次落脚，基于这个落脚点进入左右停步State
	void OnFootPlantNotify(EFootPlant Foot);
	
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	
	//是否具有加速度（移动输入）
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool HasAcceleration() const {return Acceleration > 0.f;}

	//是否处于跳跃状态（Falling + JumpCount > 0）
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsJumping() const {return  bisJumping;}

	//是否处于空中状态（Falling + JumpCount = 0）
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsFalling() const {return  bisFalling;}

	//是否处于地面状态 （Walking）
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	FORCEINLINE bool bIsWalking() const {return  bisWalking;}
	
	//是否处于步行模式
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE int IsWalkMode() const {return bWalkMode== true ? 0 : 1;}

	// -- 延迟停步系统 --
	
	// 玩家已松开移动键，锁速度等待下一个 FootPlant Notify 退出
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	bool bRequestStop = false;
	
	// FootPlantNotify停步后首次触发 且 bRequestStop==true 时设为 true，作为过渡条件进入停步状态机
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	bool bCanEnterStop = false;

	// 记录应该在哪个脚落地时停步
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Stop")
	EFootPlant PendingStopFoot = EFootPlant::None;

	// 松开移动输入时请求停步，开始锁速度并放开进入停步权限，重置停步相关条件变量
	void RequestStop();

	// 清除停步请求，重置时使用（新移动输入 / 跳跃 / 进入 Stop 状态后）
	void ClearStopRequest();

	// AnimBP 进入Stop状态时调用，消费停步请求
	UFUNCTION(BlueprintCallable,meta=(BlueprintThreadSafe))
	void OnStopStateEntered();

	// -- AnimBP Transition Rule 辅助 --
	
	//进入左停步的过渡条件（落脚点更新并确定为左脚）
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE bool CanEnterLeftStop() const { return bCanEnterStop && PendingStopFoot == EFootPlant::Left; }

	//进入右停步的过渡条件（落脚点更新并确定为右脚）
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	FORCEINLINE bool CanEnterRightStop() const { return bCanEnterStop && PendingStopFoot == EFootPlant::Right; }
	
	// -- BlendSpace 驱动参数 --
	
	//角色当前移动速度
	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	float GroundSpeed = 0.f;
	
	UPROPERTY(BlueprintReadOnly, Category="Locomotion")
	bool bIsMoving = false;
	
	// GA_Evade 确认可衔接冲刺、截断蒙太奇时置 true，ABP 据此进入 Sprint State；离开 Sprint 时经 OnSprintStateLeft 复位
	UPROPERTY(BlueprintReadOnly, Category="Locomotion|Sprint")
	bool bEvadeToSprint = false;

	// AnimBP 退出 Sprint 状态时调用
	UFUNCTION(BlueprintCallable, meta=(BlueprintThreadSafe))
	void OnSprintStateLeft(); 
	
	// ── Idle Action 系统 ─────────────────────────────────────────
	
	// 配置：一个条目对应一个待机动作变体（动画 + 持续时间）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction|Config")
	TArray<FIdleActionEntry> IdleActionEntries;

	// Idle 姿态下等待多少秒后触发随机动作
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IdleAction|Config",
		meta = (ClampMin = "0.5"))
	float IdleChangeInterval = 5.0f;

	// 0 = Idle 姿态；1..N = 正在播放对应 IdleActionEntries[Index-1] 的动作
	// ABP读取此值来切换状态
	UPROPERTY(BlueprintReadOnly, Category = "IdleAction|Runtime")
	int32 IdleActionIndex = 0;

	// 当前活跃动作的动画序列，在进入 Action 阶段时设置
	// 允许 AnimBP 使用单个 Action 状态动态绑定序列（测试使用方案）
	UPROPERTY(BlueprintReadOnly, Category = "IdleAction|Runtime")
	TObjectPtr<UAnimSequence> CurrentIdleActionSequence;
	
	
	// ──────────────────────────── WalkRun基于步幅切换BS系统    ─────────────────────────────────
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Configuration|Blend Curves")
	TObjectPtr<UCurveFloat> StrideBlend_N_Walk = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Configuration|Blend Curves")
	TObjectPtr<UCurveFloat> StrideBlend_N_Run = nullptr;
	
	UFUNCTION(BlueprintCallable, Category="Blend | Stride", meta=(BlueprintThreadSafe))
	float CalculateStrideBlend() const ;

	UFUNCTION(BlueprintCallable, Category="Blend | Stride", meta=(BlueprintThreadSafe))
	float CalculateStandingPlayRate() const ;

	float GetAnimCurveClamped(const FName& Name, float Bias, float ClampMin,float ClampMax) const;
private:
	FVector CacheVelocity = FVector::ZeroVector;
	
	float Acceleration =0 ;

	bool bWalkMode = false;
	
	bool bisFalling;
	bool bisJumping;
	bool bisWalking;
	
	UPROPERTY()
	AExtraPlayerCharacter* OwnerCharacter;

	UPROPERTY()
	UExtraGameMovementComponent* OwnerMovementComp;


	// ── Idle Action 系统（变量） ─────────────────────────────────
	
	// 统一计时器：Idle 和 Action 阶段共用，阶段切换时归零
	float IdlePhaseTimer = 0.f;

	// 上一轮选中动作的 0-based 索引，初始 -1 表示"无上一轮"
	// 跨移动周期保留，防止停下来后立即重复同一动作
	int32 PrevIdleActionIndex = -1;

	// 核心更新，由NativeUpdateAnimation在Idle状态下每帧调用
	void UpdateIdleActionSystem(float DeltaSeconds);

	// O(1) 无重复随机选取下一个动作，返回 1-based 索引（0 表示无可用动作）
	int32 PickNextIdleAction();
};

