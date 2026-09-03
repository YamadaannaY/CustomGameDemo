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

	// 恒定转向速率；空中削弱转向
	float RotationRateYaw = RotationRate.Yaw;
	if (IsFalling())
	{
		RotationRateYaw *= AirRotationScale;
	}

	// 本帧最大旋转角
	const float MaxYawThisFrame =
		RotationRateYaw * DeltaTime;

	// 取 [-180, 180] 最短旋转路径并夹到本帧上限，恒速转向到对齐为止
	const float DeltaYaw =
		FMath::FindDeltaAngleDegrees(
			CurrentRotation.Yaw,
			TargetRotation.Yaw
		);

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
