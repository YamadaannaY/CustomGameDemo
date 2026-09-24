#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtraGameMovementComponent.generated.h"

class UCurveFloat;
class UCurveVector;

/*
 *项目用CMC
 * -地面旋转默认走 ALS 式 VelocityDirection：双层插值 + 转向速率曲线（按速度档位）+ 相机转速放大
 * -bUseALSStyleRotation=false 时退回旧逻辑：转向速率随角度差自适应（小角度慢速微调/大角度快速掉头）
 * -MovementCurve 按速度档位驱动最大加速度/刹车减速度/地面摩擦（ALS MovementCurve 同款）
 *
 *注：旋转把手感拆成两层后，TargetRotation 是缓存的「中间目标」，不要用它当权威朝向读取。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraGameMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UExtraGameMovementComponent();

	virtual void BeginPlay() override;

	// 空中（跳跃/下落）时左右输入转向的削弱系数，值越小空中越难转向（0 = 完全不能转向，1 = 与地面一致）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AirRotationScale = 0.3f;

	// 总开关：false 时地面转向走下面的角度差自适应实现
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)")
	bool bEnableAdaptiveTurnRate = true;

	// 角度差 <= 此值（度）视为微调带：速率从 SlowTurnMinRate 线性升到 RotationRate.Yaw，
	// 用于轻微修正方向时柔和回正、避免甩头
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="180.0"))
	float SlowTurnAngle = 45.f;

	// 微调带最小转向速率（角度差 0° 时）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float SlowTurnMinRate = 280.f;

	// 角度差 >= 此值（度）进入快转带：速率从 RotationRate.Yaw 线性升到 FastTurnMaxRate，用于大角度快速掉头
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="180.0"))
	float FastTurnAngle = 100.f;

	// 快转带最大转向速率（角度差 180° 时）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float FastTurnMaxRate = 900.f;

	// ── ALS 式转向（VelocityDirection）──────────────────────────
	// 总开关：true 走双层插值 + 速率曲线；false 退回上面的角度差自适应
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)")
	bool bUseALSStyleRotation = true;

	// 第一层插值速率（deg/s）：把缓存的 TargetRotation 匀速推向「速度方向」。
	// 越大目标跟得越紧；ALS 的 VelocityDirection 用 800
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)", meta=(ClampMin="0.0"))
	float TargetRotationInterpSpeed = 800.f;

	// 第二层插值用的转向速率曲线：横轴 = 映射速度（0 停 / 1 走 / 2 跑 / 3 冲刺），纵轴 = deg/s。
	// 为空时退回 RotationRate.Yaw
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)")
	TObjectPtr<UCurveFloat> RotationRateCurve;

	// 速度高于此值（cm/s）才刷新「最后速度方向」，避免停步瞬间方向乱飘（ALS 用 1）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)", meta=(ClampMin="0.0"))
	float MovingSpeedThreshold = 1.f;

	// 相机转速放大：相机 yaw 变化率达到 MaxCameraYawRate 时，转向速率放大到 MaxCameraYawRateMultiplier 倍。
	// 甩镜头越快角色跟得越紧，是「跟手」的来源（对应 ALS 的 AimYawRate，这里只取相机转速，不涉及瞄准）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)", meta=(ClampMin="0.0"))
	float MaxCameraYawRate = 300.f;

	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)", meta=(ClampMin="1.0"))
	float MaxCameraYawRateMultiplier = 3.f;

	// 有 montage 播放（攻击/闪避/急停/转身）期间不参与朝向：
	// 这些情形由动画的根运动 + MotionWarping 驱动朝向，而本组件的朝向写入发生在根运动应用之后，
	// 不跳过会覆盖掉 MW 写入的朝向。
	// 判定刻意不用 HasAnyRootMotion()：AnimBP 的 Root Motion Mode 设为 RootMotionFromEverything
	// 时它恒为 true，会把地面转向整个关掉
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)")
	bool bSkipRotationDuringRootMotion = true;

	// 排查用：每帧在屏幕上打印转向判定的关键数值。手感定稿后关掉
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (ALS Rotation)")
	bool bDebugRotation = false;

	// ── 移动曲线（ALS MovementCurve 同款）────────────────────────
	// 横轴 = 映射速度（0..3），X = 最大加速度，Y = 刹车减速度，Z = 地面摩擦
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Movement Curve)")
	TObjectPtr<UCurveVector> MovementCurve;

	// 三档速度锚点，用于把当前速度映射到 0..3 的曲线横轴。
	// AExtraPlayerCharacter 的 BeginPlay 会用自身配置覆盖这三个值，保证单一数据源
	void SetGaitSpeeds(float InWalk, float InRun, float InSprint);

	// 停步请求（松手后、等落脚点进入停步动画的窗口）开关，由 AnimInstance 在停步状态变化时同步。
	// 该窗口内改用 StopBrakingDeceleration 刹车，让「松手滑多远」独立于速度曲线可调
	void SetStopRequested(bool bRequested) { bStopRequested = bRequested; }

	// 停步窗口内的刹车减速度（cm/s²）。值越小滑得越远：松手速度 v 的滑行距离约为 v²/(2*该值)
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Stop)", meta=(ClampMin="0.0"))
	float StopBrakingDeceleration = 1000.f;

	float GetMappedSpeed() const;

	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void PhysWalking(float DeltaTime, int32 Iterations) override;

	// 转向：地面默认 ALS 式双层插值，空中按 AirRotationScale 削弱
	virtual void PhysicsRotation(float DeltaTime) override;

private:
	// ALS 式地面转向主体（在 PhysicsRotation 里于根运动门控之后调用）
	void PhysicsRotationALS(float DeltaTime);

	// 旧实现：按角度差自适应速率的地面转向 + 空中削弱转向
	void PhysicsRotationLegacy(float DeltaTime);

	// 双层插值：TargetRotation 匀速逼近目标，角色再指数逼近 TargetRotation
	void SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed, float ActorInterpSpeed, float DeltaTime);

	// 本帧第二层插值速率（deg/s）= 速率曲线(映射速度) × 相机转速放大
	float CalculateGroundedRotationRate() const;

	// 旧逻辑：由角度差（0..180 度）线性映射出本帧转向速率（deg/s）
	float GetAdaptiveTurnRate(float AbsDeltaYaw, float BaseYawRate) const;

	// 本帧相机 yaw 变化率（deg/s），无 Controller（模拟代理等）时为 0
	float GetCameraYawRate() const { return CachedCameraYawRate; }

	float GaitWalkSpeed = 250.f;
	float GaitRunSpeed = 600.f;
	float GaitSprintSpeed = 800.f;

	// 两层插值里被平滑的「中间目标」朝向
	FRotator TargetRotation = FRotator::ZeroRotator;

	// 最后一次有效速度方向：停步 / 速度过低时沿用，避免目标方向抖动
	FRotator LastVelocityRotation = FRotator::ZeroRotator;

	float LastCameraYaw = 0.f;
	float CachedCameraYawRate = 0.f;

	// 停步窗口标记，见 SetStopRequested
	bool bStopRequested = false;
};
