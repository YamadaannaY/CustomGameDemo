#include "ExtraGameAnimInstance.h"
#include "ExtraGameMovementComponent.h"
#include "ExtraPlayerCharacter.h"


void UExtraGameAnimInstance::OnFootPlantNotify(EFootPlant Foot)
{
	if (!bRequestStop)
		return;
	
	PendingStopFoot = Foot;
	bCanEnterStop = true;
}

void UExtraGameAnimInstance::RequestStop()
{
	bRequestStop = true;
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;

	// 停步时退出 Sprint 状态
	bIsSprinting = false;
}

void UExtraGameAnimInstance::ClearStopRequest()
{
	bRequestStop = false;
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
}

void UExtraGameAnimInstance::OnStopStateEntered()
{
	bRequestStop = false;
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
}


void UExtraGameAnimInstance::OnSprintStateLeft()
{
	bIsSprinting = false;
	bEvadeToSprint = false;
}

void UExtraGameAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwnerCharacter=Cast<AExtraPlayerCharacter>(TryGetPawnOwner());
	if (OwnerCharacter)
	{
		OwnerMovementComp=Cast<UExtraGameMovementComponent>(OwnerCharacter->GetCharacterMovement());
	}
}

void UExtraGameAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (OwnerMovementComp)
	{
		bisFalling=OwnerMovementComp->IsFalling() && OwnerCharacter->JumpCurrentCount==0 ;
		bisWalking = OwnerMovementComp->IsWalking();
		bisJumping = OwnerMovementComp->IsFalling() && OwnerCharacter->JumpCurrentCount > 0 ;
		bWalkMode =OwnerCharacter->GetWalkMode();

		MovementMode = OwnerMovementComp->MovementMode;

		Acceleration = OwnerMovementComp->GetCurrentAcceleration().Length();
	}

	if (bisFalling || bisJumping)
	{
		ClearStopRequest();
	}

	if (OwnerCharacter && OwnerMovementComp)
	{
		if (!bRequestStop)
		{
			CacheVelocity = OwnerCharacter->GetVelocity();
			GroundSpeed = CacheVelocity.Size2D();
		}
		else if (bEvadeToSprint)
		{
			CacheVelocity = OwnerCharacter->GetVelocity();
			GroundSpeed = CacheVelocity.Size2D();
			bIsSprinting = true;
			bEvadeToSprint = false;
		}
		else
		{
			OwnerMovementComp->Velocity = CacheVelocity;
		}
		bIsMoving = GroundSpeed > 3.f && OwnerMovementComp->IsMovingOnGround();
		
		if (!bRequestStop)
		{
			const float MaxSpeed = bIsSprinting
				? OwnerCharacter->GetCharacterMovement()->MaxWalkSpeed * 2.f
				: OwnerMovementComp->MaxWalkSpeed;
			Stride = (MaxSpeed > 0.f) ? FMath::Clamp(GroundSpeed * StrideCoefficient / MaxSpeed, 0.f, 1.f) : 0.f;
		}

		WalkRunBlend = bIsSprinting ? 1.f : 0.f;

		if (bIsMoving)
		{
			const FRotator ActorRot = OwnerCharacter->GetActorRotation();
			const FVector VelDir = CacheVelocity.GetSafeNormal2D();
			const FVector ForwardDir = ActorRot.Vector().GetSafeNormal2D();
			const FVector RightDir = FRotationMatrix(ActorRot).GetScaledAxis(EAxis::Y);

			const float DotForward = FVector::DotProduct(ForwardDir, VelDir);
			const float DotRight = FVector::DotProduct(RightDir, VelDir);
			MoveDirection = FMath::RadiansToDegrees(FMath::Atan2(DotRight, DotForward));
		}
		else
		{
			MoveDirection = 0.f;
		}
	}

	UpdateIdleActionSystem(DeltaSeconds);
}

void UExtraGameAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
}

int32 UExtraGameAnimInstance::PickNextIdleAction()
{
	const int32 N = IdleActionEntries.Num();
	if (N == 0) return 0;

	if (N == 1) return 1;

	int32 RandomIdx = FMath::RandRange(0, N - 2);
	if (RandomIdx >= PrevIdleActionIndex)
	{
		++RandomIdx;
	}

	PrevIdleActionIndex = RandomIdx;
	return RandomIdx + 1;
}

void UExtraGameAnimInstance::UpdateIdleActionSystem(float DeltaSeconds)
{
	if (bIsMoving || bisFalling || bisJumping)
	{
		IdlePhaseTimer = 0.f;
		IdleActionIndex = 0;
		CurrentIdleActionSequence = nullptr;
		return;
	}

	const int32 NumActions = IdleActionEntries.Num();
	if (NumActions == 0)
	{
		return;
	}

	IdlePhaseTimer += DeltaSeconds;

	if (IdleActionIndex == 0)
	{
		if (IdlePhaseTimer >= IdleChangeInterval)
		{
			IdleActionIndex = PickNextIdleAction();
			IdlePhaseTimer = 0.f;

			const int32 ArrayIdx = IdleActionIndex - 1;
			if (IdleActionEntries.IsValidIndex(ArrayIdx))
			{
				CurrentIdleActionSequence = IdleActionEntries[ArrayIdx].AnimSequence;
			}
		}
	}
	else
	{
		const int32 ArrayIdx = IdleActionIndex - 1;
		const float ActionDuration = IdleActionEntries.IsValidIndex(ArrayIdx)
			? IdleActionEntries[ArrayIdx].Duration
			: 10.0f;

		if (IdlePhaseTimer >= ActionDuration)
		{
			IdleActionIndex = 0;
			IdlePhaseTimer = 0.f;
			CurrentIdleActionSequence = nullptr;
		}
	}
}