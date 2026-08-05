// Fill out your copyright notice in the Description page of Project Settings.


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
}

void UExtraGameAnimInstance::ClearStopRequest()
{
	bRequestStop = false;
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
}

void UExtraGameAnimInstance::OnStopStateEntered()
{
	// 进入 Stop 状态后消费请求，防止重复触发
	bRequestStop = false;
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
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
	
	// 离地时清除停步请求，防止落地后误触发 Stop
	if (bisFalling || bisJumping)
	{
		ClearStopRequest();
	}

	// -- BlendSpace 驱动参数 --
	if (OwnerCharacter && OwnerMovementComp)
	{
		if (!bRequestStop)
		{
			CacheVelocity = OwnerCharacter->GetVelocity();
			GroundSpeed = CacheVelocity.Size2D();
		}
		else
		{
			OwnerMovementComp->Velocity = CacheVelocity ; 
		}
		bIsMoving = GroundSpeed > 3.f && OwnerMovementComp->IsMovingOnGround();

		if (!bRequestStop )
		{
			// Stride：归一化到 MaxWalkSpeed
			const float MaxSpeed = OwnerMovementComp->MaxWalkSpeed;
			Stride = (MaxSpeed > 0.f) ? FMath::Clamp(GroundSpeed / MaxSpeed, 0.f, 1.f) : 0.f;
		}

		// WalkRunBlend：暂固定为 0（Walk 行），后续可扩展为冲刺驱动
		WalkRunBlend = 0.f;

		// MoveDirection：速度方向相对角色朝向的角度
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

	// -- Idle Action System --
	UpdateIdleActionSystem(DeltaSeconds);
}

void UExtraGameAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
}

// ── Idle Action System ─────────────────────────────────────────────────

int32 UExtraGameAnimInstance::PickNextIdleAction()
{
	const int32 N = IdleActionEntries.Num();
	if (N == 0) return 0;

	// 只有一个动作时始终选它
	if (N == 1) return 1;

	// O(1) 无重复随机选取：
	// 从 [0, N-2] 中选取，若结果 >= 排除索引则 +1，均匀映射到 [0, N-1] \ {excluded}
	int32 RandomIdx = FMath::RandRange(0, N - 2);
	if (RandomIdx >= PrevIdleActionIndex)
	{
		++RandomIdx;
	}

	PrevIdleActionIndex = RandomIdx;
	return RandomIdx + 1; // 转为 1-based 给 AnimBP
}

void UExtraGameAnimInstance::UpdateIdleActionSystem(float DeltaSeconds)
{
	// Guard：只在站立不动时运行，移动/下落/跳跃时重置为 Idle
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
		// ── Phase 0: Idle ─────────────────────────────────────
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
		// ── Phase 1..N: Action ────────────────────────────────
		const int32 ArrayIdx = IdleActionIndex - 1;
		const float ActionDuration = IdleActionEntries.IsValidIndex(ArrayIdx)
			? IdleActionEntries[ArrayIdx].Duration
			: 2.0f; // 兜底值

		if (IdlePhaseTimer >= ActionDuration)
		{ 
			// 播完 → 回到常驻 Idle
			IdleActionIndex = 0;
			IdlePhaseTimer = 0.f;
			CurrentIdleActionSequence = nullptr;
		}
	}
}
