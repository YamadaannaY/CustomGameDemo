#include "ExtraGameMovementComponent.h"

#include "GameFramework/Character.h"


UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


void UExtraGameMovementComponent::BeginPlay()
{
	Super::BeginPlay();
}


void UExtraGameMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
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

	if (Acceleration.IsNearlyZero(0.001f))
	{
		return;
	}

	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation();
	FRotator TargetRotation = Acceleration.Rotation();
	TargetRotation.Pitch = 0.f;
	TargetRotation.Roll = 0.f;
	CurrentRotation.Pitch = 0.f;
	CurrentRotation.Roll = 0.f;

	if (CurrentRotation.Equals(TargetRotation, 0.1f))
	{
		return;
	}

	const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetRotation.Yaw);
	const float AbsDeltaYaw = FMath::Abs(DeltaYaw);

	const float SpeedBlend = FMath::Clamp(AbsDeltaYaw / 180.f, 0.f, 1.f);
	float CurrentRotationRate = FMath::Lerp(MinRotationRate, MaxRotationRate, SpeedBlend);

	// 空中（跳跃/下落）时削弱转向幅度，仅允许小幅度的左右朝向旋转
	if (IsFalling())
	{
		CurrentRotationRate *= AirRotationScale;
	}

	const float MaxYawThisFrame = CurrentRotationRate * DeltaTime;
	float StepYaw = FMath::Clamp(DeltaYaw, -MaxYawThisFrame, MaxYawThisFrame);

	FRotator DesiredRotation = CurrentRotation;
	DesiredRotation.Yaw = CurrentRotation.Yaw + StepYaw;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}

