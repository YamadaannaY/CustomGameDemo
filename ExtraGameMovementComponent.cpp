#include "ExtraGameMovementComponent.h"

UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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

	// 没有有效加速度，不旋转
	if (Acceleration.IsNearlyZero(0.001f))
	{
		return;
	}

	const FRotator CurrentRotation =
		UpdatedComponent->GetComponentRotation();

	// 根据移动加速度计算目标方向
	FRotator TargetRotation = Acceleration.Rotation();
	TargetRotation.Pitch = 0.f;
	TargetRotation.Roll = 0.f;

	// 取 [-180, 180] 最短旋转路径
	const float DeltaYaw =
		FMath::FindDeltaAngleDegrees(
			CurrentRotation.Yaw,
			TargetRotation.Yaw
		);

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
	const float MaxYawThisFrame =
		TurnRateYaw * DeltaTime;

	const float StepYaw =
		FMath::Clamp(
			DeltaYaw,
			-MaxYawThisFrame,
			MaxYawThisFrame
		);

	FRotator DesiredRotation = CurrentRotation;
	DesiredRotation.Yaw += StepYaw;
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;

	MoveUpdatedComponent(
		FVector::ZeroVector,
		DesiredRotation,
		false
	);
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
