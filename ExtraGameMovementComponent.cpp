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

	if (HasRootMotionSources() || CharacterOwner->IsPlayingRootMotion())
	{
		Super::PhysicsRotation(DeltaTime);
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

	float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetRotation.Yaw);
	float AbsDeltaYaw = FMath::Abs(DeltaYaw);

	float SpeedBlend = FMath::Clamp(AbsDeltaYaw / 180.f, 0.f, 1.f);
	float CurrentRotationRate = FMath::Lerp(MinRotationRate, MaxRotationRate, SpeedBlend);

	float MaxYawThisFrame = CurrentRotationRate * DeltaTime;
	float StepYaw = FMath::Clamp(DeltaYaw, -MaxYawThisFrame, MaxYawThisFrame);

	FRotator DesiredRotation = CurrentRotation;
	DesiredRotation.Yaw = CurrentRotation.Yaw + StepYaw;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}

