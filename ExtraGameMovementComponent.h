#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtraGameMovementComponent.generated.h"

class UCurveFloat;
class UCurveVector;

/*
 *  项目用CMC
 * -旋转逻辑：双层插值（目标朝向匀速逼近最后一次有效速度朝向+角色朝向指数逼近目标朝向） + 转向速率曲线（按速度档位读取）+ 相机转速放大（相机速度越大，转向速度越快以实现跟手效果）
 * -MovementCurve 按速度档位驱动最大加速度/刹车减速度/地面摩擦
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraGameMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UExtraGameMovementComponent();

	virtual void BeginPlay() override;

	// 空中（跳跃/下落）时转向的削弱系数，缩放两层插值速率：1 = 与地面完全一致，越小空中越难转向（线性）。
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.01", ClampMax="1.0"))
	float AirRotationScale = 0.3f;
	
	// 第一层插值速率（deg/s）：把缓存的 TargetRotation 匀速推向「速度方向」。越大目标跟得越紧
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float TargetRotationInterpSpeed = 800.f;

	// 第二层插值用的转向速率曲线：横轴 = 映射速度（0 停 / 1 走 / 2 跑 / 3 冲刺），纵轴 = deg/s。
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)")
	TObjectPtr<UCurveFloat> RotationRateCurve;

	// 速度高于此值（cm/s）才刷新「最后输入方向」，低于此值可以看做已经没有输入了，将此刻输入朝向作为输入朝向
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float MovingSpeedRefreshThreshold = 50.f;

	// 相机转速放大：相机 yaw 变化率达到 MaxCameraYawRate 时，转向速率放大到 MaxCameraYawRateMultiplier 倍。
	// 甩镜头越快角色跟得越紧，是「跟手」的来源
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float MaxCameraYawRate = 300.f;

	//将相机转速放大区间落到1-3之间
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="1.0"))
	float MaxCameraYawRateMultiplier = 3.f;
	
	// 排查用：每帧在屏幕上打印转向判定的关键数值。
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)")
	bool bDebugRotation = false;

	// ── 移动曲线（MovementCurve）────────────────────────
	// 横轴 = 映射速度（0..3），根据Mapped后的速度区间值读其纵轴，其中：X = 加速度，Y = 减速度，Z = 地面摩擦
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Movement Curve)")
	TObjectPtr<UCurveVector> MovementCurve;

	// 三档移动模式的速度值获取，用于把当前速度映射到 0..3 的曲线横轴，在角色类BeginPlay中调用填入配置好的速度
	void SetGaitSpeeds(float InWalk, float InRun, float InSprint);

	// 停步请求（松手后、等落脚点进入停步动画的窗口）开关，由 AnimInstance 在停步状态变化时同步。
	void SetStopRequested(bool bRequested) { bStopRequested = bRequested; }

	// 停步窗口内的刹车减速度（cm/s²）。值越小滑得越远：松手速度 v 的滑行距离约为 v²/(2*该值)
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Stop)", meta=(ClampMin="0.0"))
	float StopBrakingDeceleration = 500.f;

	// ── 服务器端朝向跟随 ──────────────────────────────────────────
	// 服务器上本地控制角色（autonomous proxy）的朝向改由客户端主导：客户端每帧经
	// Server_SyncClientYaw 上报算好的朝向。这个函数在ServerRPC中调用
	void SetClientAuthoritativeYaw(float InYaw)
	{
		ClientAuthoritativeYaw = InYaw;
		bHasClientAuthoritativeYaw = true;
	}

	// 服务器端朝向收口速率（deg/s）：Unreliable 上报间隔不均是常态，
	// 直接硬设会抖，用一个远快于上报周期的速率平滑收口
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float ServerYawFollowRate = 1440.f;

	// 把当前速度映射到 0..3的区间内：0 = 停，1 = 走，2 = 跑，3 = 冲刺
	// 这样做的意义是为了换速度数据时曲线不用重画
	float GetMappedSpeed() const;

	/*根据当前速度被Mapped后的值作为曲线横轴值，读出对应纵轴值值*/
	
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void PhysWalking(float DeltaTime, int32 Iterations) override;
	
	virtual void PhysicsRotation(float DeltaTime) override;
private:
	//地面转向主体核心
	void CustomPhysicsRotation(float DeltaTime);
	
	// 双层插值：TargetRotation 匀速逼近目标，角色再指数逼近 TargetRotation
	void SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed, float ActorInterpSpeed, float DeltaTime);

	// 本帧第二层插值速率（deg/s）= 速率曲线(映射速度) × 相机转速放大
	float CalculateGroundedRotationRate() const;
	
	// 本帧相机 yaw 变化率（deg/s）
	float GetCameraYawRate() const { return CachedCameraYawRate; }

	float GaitWalkSpeed = 250.f;
	float GaitRunSpeed = 600.f;
	float GaitSprintSpeed = 800.f;

	// 两层插值里被平滑的「中间目标」朝向
	FRotator TargetRotation = FRotator::ZeroRotator;

	// 最后一次有效输入方向：无输入 / 速度过低时沿用，避免松手瞬间目标方向乱飘
	FRotator LastRotationTarget = FRotator::ZeroRotator;

	float LastCameraYaw = 0.f;
	float CachedCameraYawRate = 0.f;

	// 停步窗口标记，见 SetStopRequested
	bool bStopRequested = false;

	// 客户端每帧算好的Yaw值
	float ClientAuthoritativeYaw = 0.f;
	bool bHasClientAuthoritativeYaw = false;
};
