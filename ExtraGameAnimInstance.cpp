#include "ExtraGameAnimInstance.h"
#include "ExtraGameMovementComponent.h"
#include "ExtraPlayerCharacter.h"


void UExtraGameAnimInstance::OnFootPlantNotify(EFootPlant Foot)
{
	// 本地：松手（bRequestStop）等待 FootPlant 进入停步状态。
	// 模拟端（其他客户端看到的角色）没有 bRequestStop（只在拥有客户端的输入回调里设置），
	// 若不处理永远进不了左右停步、卡在跑步态回不到 idle。
	// 模拟端改为：速度降到 SimProxyStopSpeedThreshold 以下时同样响应 FootPlant，进入停步状态。
	const bool bSimProxyStopping = OwnerCharacter && OwnerCharacter->GetLocalRole() == ROLE_SimulatedProxy
		&& OwnerMovementComp && OwnerMovementComp->Velocity.Size2D() < SimProxyStopSpeedThreshold;

	if (!bRequestStop && !bSimProxyStopping)
	{
		return;
	}

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
	ClearStopAndVelocity();
}

void UExtraGameAnimInstance::ClearStopAndVelocity()
{
	ClearStopRequest();

	// 停步/转身到位：清零残留速度。松手后 NativeUpdateAnimation 的 bRequestStop 分支
	// 会把 Velocity 恒锁在 CacheVelocity（松手速度），若不在此清零，
	// CharacterMovement 会继续用残留速度滑行，表现为「停步/转身后 idle 仍向前移动」。
	CacheVelocity = FVector::ZeroVector;
	if (OwnerMovementComp)
	{
		OwnerMovementComp->Velocity = FVector::ZeroVector;
	}
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

		// 模拟端（其他客户端看到的角色）没有本地输入，加速度依赖服务器复制；
		// 若复制不可用或为 0，但仍有速度，说明在移动——用速度兜底为有加速度，
		// 保证 AnimBP 的 HasAcceleration() 过渡条件在远端也能成立（不会以 idle 状态移动）。
		float AccelLen = OwnerMovementComp->GetCurrentAcceleration().Length();
		if (OwnerCharacter && OwnerCharacter->GetLocalRole() == ROLE_SimulatedProxy)
		{
			if (AccelLen < KINDA_SMALL_NUMBER && !OwnerMovementComp->Velocity.IsNearlyZero())
			{
				AccelLen = 1.f;
			}
		}
		Acceleration = AccelLen;
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
			// 停步锁速：松手后速度保持 CacheVelocity，等待 FootPlant 进入停步状态机。
			// 注意：这是客户端独有状态，服务器端 bRequestStop 恒为 false 不会锁速——
			// 但角色位置以服务器权威为准，锁速只影响本地动画表现，不参与网络位置裁决。
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