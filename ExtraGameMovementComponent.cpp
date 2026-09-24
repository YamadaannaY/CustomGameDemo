#include "ExtraGameMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"

namespace
{
	// 把 Value 从 [InMin, InMax] 线性映射到 [OutMin, OutMax]，并夹在输出区间内
	float MapRangeClamped(float Value, float InMin, float InMax, float OutMin, float OutMax)
	{
		if (InMax <= InMin)
		{
			return OutMin;
		}

		const float T = FMath::Clamp((Value - InMin) / (InMax - InMin), 0.f, 1.f);
		return FMath::Lerp(OutMin, OutMax, T);
	}

	// 已松手、但速度仍高于此值时也继续回正朝向（ALS 用的就是 150）
	constexpr float FallbackRotationSpeed = 150.f;
}

UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UExtraGameMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// 两层插值的缓存初值对齐当前朝向，否则开局第一帧会从零朝向硬拽过来
	if (CharacterOwner)
	{
		TargetRotation = FRotator(0.f, CharacterOwner->GetActorRotation().Yaw, 0.f);
		LastVelocityRotation = TargetRotation;
	}

	if (const AController* OwnerController = CharacterOwner ? CharacterOwner->GetController() : nullptr)
	{
		LastCameraYaw = OwnerController->GetControlRotation().Yaw;
	}
}

void UExtraGameMovementComponent::SetGaitSpeeds(float InWalk, float InRun, float InSprint)
{
	GaitWalkSpeed = InWalk;
	GaitRunSpeed = InRun;
	GaitSprintSpeed = InSprint;
}

float UExtraGameMovementComponent::GetMappedSpeed() const
{
	// 把当前速度映射到 0..3：0 = 停，1 = 走，2 = 跑，3 = 冲刺。
	// 用锚点归一化而不是直接取速度值，是为了换速度配置时曲线不用重画
	const float Speed = Velocity.Size2D();

	if (GaitRunSpeed <= GaitWalkSpeed || GaitSprintSpeed <= GaitRunSpeed)
	{
		// 锚点配置非法（未同步或填错），退化为按跑速归一
		return (GaitRunSpeed > 0.f) ? FMath::Min(Speed / GaitRunSpeed, 3.f) : 0.f;
	}

	if (Speed > GaitRunSpeed)
	{
		return MapRangeClamped(Speed, GaitRunSpeed, GaitSprintSpeed, 2.0f, 3.0f);
	}

	if (Speed > GaitWalkSpeed)
	{
		return MapRangeClamped(Speed, GaitWalkSpeed, GaitRunSpeed, 1.0f, 2.0f);
	}

	return MapRangeClamped(Speed, 0.0f, GaitWalkSpeed, 0.0f, 1.0f);
}

float UExtraGameMovementComponent::GetMaxAcceleration() const
{
	// 按当前速度档位取最大加速度，走/跑/冲刺各有一套加减速手感
	if (!IsMovingOnGround() || !MovementCurve)
	{
		return Super::GetMaxAcceleration();
	}

	return MovementCurve->GetVectorValue(GetMappedSpeed()).X;
}

float UExtraGameMovementComponent::GetMaxBrakingDeceleration() const
{
	// 停步窗口内用专用刹车：这段的速度衰减直接决定「松手滑多远」，必须独立于速度曲线可调，
	// 否则改了 MovementCurve 的 Y 值会连带改变停步手感
	if (bStopRequested && IsMovingOnGround())
	{
		return StopBrakingDeceleration;
	}

	if (!IsMovingOnGround() || !MovementCurve)
	{
		return Super::GetMaxBrakingDeceleration();
	}

	return MovementCurve->GetVectorValue(GetMappedSpeed()).Y;
}

void UExtraGameMovementComponent::PhysWalking(float DeltaTime, int32 Iterations)
{
	if (MovementCurve)
	{
		GroundFriction = MovementCurve->GetVectorValue(GetMappedSpeed()).Z;
	}

	Super::PhysWalking(DeltaTime, Iterations);
}

void UExtraGameMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!HasValidData() || !CharacterOwner)
	{
		return;
	}

	if (!bOrientRotationToMovement)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	// 动画/蒙太奇接管朝向时让路：本方法在根运动应用之后执行，不让路会覆盖掉 MotionWarping 写入的朝向。
	// 判定不能用 ACharacter::HasAnyRootMotion()：AnimBP 的 Root Motion Mode 一旦是
	// RootMotionFromEverything，它的 IsPlayingRootMotion() 恒为 true，会把地面转向整个关掉。
	// 改成只看「有没有 montage 在播」—— 攻击/闪避/急停/转身正需要 MW 接管朝向，跑步时没有 montage
	const UAnimInstance* AnimInst = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const bool bAnimDrivingRotation = AnimInst && AnimInst->IsAnyMontagePlaying();

	if (bSkipRotationDuringRootMotion && bAnimDrivingRotation)
	{
		if (bDebugRotation && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				static_cast<int32>(CharacterOwner->GetUniqueID()) + 1, 0.f, FColor::Red,
				TEXT("[ALS Rot] 被蒙太奇门控拦截（bSkipRotationDuringRootMotion，有 montage 在播）"));
		}

		TargetRotation = FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
		return;
	}

	// 「最后速度方向」是各分支共用的缓存：速度过低时沿用上一次的有效方向，避免停步瞬间方向乱飘。
	// 用 CMC 的 Velocity 而不是 UpdatedComponent->GetComponentVelocity()：后者靠
	// UpdateComponentVelocity() 同步，时机不保证，可能整帧读到 0 导致朝向永远不更新
	if (Velocity.Size2D() > MovingSpeedThreshold)
	{
		LastVelocityRotation = FRotator(0.f, Velocity.ToOrientationRotator().Yaw, 0.f);
	}

	if (bUseALSStyleRotation && !IsFalling())
	{
		PhysicsRotationALS(DeltaTime);
		return;
	}

	// 没有有效加速度，不旋转
	if (Acceleration.IsNearlyZero(0.001f))
	{
		return;
	}

	PhysicsRotationLegacy(DeltaTime);
}

void UExtraGameMovementComponent::PhysicsRotationALS(float DeltaTime)
{
	// 刷新相机转速（供速率放大用）。模拟代理上拿不到 Controller，退化为 0 即不放大
	CachedCameraYawRate = 0.f;
	if (const AController* OwnerController = CharacterOwner->GetController())
	{
		const float CurrentCameraYaw = OwnerController->GetControlRotation().Yaw;
		CachedCameraYawRate = FMath::Abs(
			FMath::FindDeltaAngleDegrees(LastCameraYaw, CurrentCameraYaw)
			/ FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		LastCameraYaw = CurrentCameraYaw;
	}

	const float Speed = Velocity.Size2D();
	const bool bIsMoving = Speed > MovingSpeedThreshold;
	const bool bHasMovementInput = !Acceleration.IsNearlyZero(0.001f);
	const bool bShouldRotate = (bIsMoving && bHasMovementInput) || Speed > FallbackRotationSpeed;

	const float GroundedRate = CalculateGroundedRotationRate();

	if (bDebugRotation && CharacterOwner && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			static_cast<int32>(CharacterOwner->GetUniqueID()), 0.f, FColor::Yellow,
			FString::Printf(
				TEXT("[ALS Rot] Spd=%.0f Accel=%.0f Rotate=%d Rate=%.1f CurveVal=%.1f Mapped=%.2f TargetYaw=%.1f ActorYaw=%.1f LastVelYaw=%.1f"),
				Speed, Acceleration.Size(), bShouldRotate ? 1 : 0, GroundedRate,
				RotationRateCurve ? RotationRateCurve->GetFloatValue(GetMappedSpeed()) : -1.f,
				GetMappedSpeed(), TargetRotation.Yaw, CharacterOwner->GetActorRotation().Yaw,
				LastVelocityRotation.Yaw));
	}

	// 对齐 ALS：要有速度且仍有输入才转向；已松手但速度还快时也允许回正
	if (!bShouldRotate)
	{
		return;
	}

	SmoothCharacterRotation(LastVelocityRotation, TargetRotationInterpSpeed, GroundedRate, DeltaTime);
}

void UExtraGameMovementComponent::PhysicsRotationLegacy(float DeltaTime)
{
	const FRotator CurrentRotation = UpdatedComponent->GetComponentRotation();

	// 根据移动加速度计算目标方向
	FRotator Target = Acceleration.Rotation();
	Target.Pitch = 0.f;
	Target.Roll = 0.f;

	// 取 [-180, 180] 最短旋转路径
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, Target.Yaw);

	// 转向速率：
	// - 地面默认按角度差自适应（小角度慢/大角度快）；关闭总开关时退化为恒定 RotationRate.Yaw
	// - 空中恒定 RotationRate.Yaw 并按 AirRotationScale 削弱（与原逻辑一致）
	float TurnRateYaw = RotationRate.Yaw;
	if (!IsFalling() && bEnableAdaptiveTurnRate)
	{
		TurnRateYaw = GetAdaptiveTurnRate(FMath::Abs(DeltaYaw), RotationRate.Yaw);
	}
	else if (IsFalling())
	{
		TurnRateYaw *= AirRotationScale;
	}

	// 本帧最大旋转角，夹到剩余角度内（转向到对齐为止）
	const float MaxYawThisFrame = TurnRateYaw * DeltaTime;

	const float StepYaw = FMath::Clamp(DeltaYaw, -MaxYawThisFrame, MaxYawThisFrame);

	FRotator DesiredRotation = CurrentRotation;
	DesiredRotation.Yaw += StepYaw;
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}

void UExtraGameMovementComponent::SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed,
                                                           float ActorInterpSpeed, float DeltaTime)
{
	// 第一层：中间目标匀速逼近外部目标，起手/变向都不会有硬拐点
	TargetRotation = FMath::RInterpConstantTo(TargetRotation, Target, DeltaTime, TargetInterpSpeed);

	// 第二层：角色指数逼近中间目标（起步快、收尾慢）
	FRotator DesiredRotation = FMath::RInterpTo(
		UpdatedComponent->GetComponentRotation(), TargetRotation, DeltaTime, ActorInterpSpeed);

	// 只吃 Yaw，避免俯仰/翻滚漂移进来
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}

float UExtraGameMovementComponent::CalculateGroundedRotationRate() const
{
	// 曲线缺失 / 该速度档位取到 0 时退回 RotationRate.Yaw。
	// 注意 RInterpTo 的 FRotator 重载是线性语义（每帧走 Rate*DeltaTime 比例的差值），
	// 曲线纵轴要填 deg/s 量级（ALS 用几百），填成 0..1 的归一化值会让角色几乎不转
	float BaseRate = RotationRate.Yaw;
	if (RotationRateCurve)
	{
		const float CurveValue = RotationRateCurve->GetFloatValue(GetMappedSpeed());
		if (CurveValue > 0.f)
		{
			BaseRate = CurveValue;
		}
	}

	if (MaxCameraYawRate <= 0.f)
	{
		return BaseRate;
	}

	// 相机甩得越快，角色跟得越快
	const float CameraYawRateScale = MapRangeClamped(
		GetCameraYawRate(), 0.f, MaxCameraYawRate, 1.f, FMath::Max(MaxCameraYawRateMultiplier, 1.f));

	return BaseRate * CameraYawRateScale;
}

float UExtraGameMovementComponent::GetAdaptiveTurnRate(float AbsDeltaYaw, float BaseYawRate) const
{
	// 参数非法（SlowTurnAngle >= FastTurnAngle）时退回恒定速率
	if (SlowTurnAngle >= FastTurnAngle)
	{
		return BaseYawRate;
	}

	if (AbsDeltaYaw <= SlowTurnAngle)
	{
		// 微调带：0..SlowTurnAngle，速率从 SlowTurnMinRate 线性升到基准
		const float T = (SlowTurnAngle > 0.f)
			? FMath::Clamp(AbsDeltaYaw / SlowTurnAngle, 0.f, 1.f)
			: 1.f;
		return FMath::Lerp(FMath::Min(SlowTurnMinRate, BaseYawRate), BaseYawRate, T);
	}

	if (AbsDeltaYaw >= FastTurnAngle)
	{
		// 快转带：FastTurnAngle..180，速率从基准线性升到 FastTurnMaxRate
		const float T = FMath::Clamp(
			(AbsDeltaYaw - FastTurnAngle) / (180.f - FastTurnAngle),
			0.f, 1.f);
		return FMath::Lerp(BaseYawRate, FMath::Max(FastTurnMaxRate, BaseYawRate), T);
	}

	// 中段（SlowTurnAngle < da < FastTurnAngle）：保持基准速率
	return BaseYawRate;
}
