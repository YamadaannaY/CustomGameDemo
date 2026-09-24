#include "ExtraGameAnimInstance.h"
#include "ExtraGameMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "ExtraPlayerCharacter.h"

static const FName NAME_W_Gait(TEXT("W_Gait"));

void UExtraGameAnimInstance::OnFootPlantNotify(EFootPlant Foot)
{
	if (!bRequestStop)
		return;
	
	PendingStopFoot = Foot;
	bCanEnterStop = true;
}

void UExtraGameAnimInstance::RequestStop()
{
	SetStopRequest(true);
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
	StopRequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void UExtraGameAnimInstance::ClearStopRequest()
{
	SetStopRequest(false);
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
}

void UExtraGameAnimInstance::OnStopStateEntered()
{
	SetStopRequest(false);
	bCanEnterStop = false;
	PendingStopFoot = EFootPlant::None;
}

void UExtraGameAnimInstance::SetStopRequest(bool bRequested)
{
	bRequestStop = bRequested;

	// 停步窗口期间移动组件改用停步专用刹车，减速交回 CMC 而不是冻结速度
	if (OwnerMovementComp)
	{
		OwnerMovementComp->SetStopRequested(bRequested);
	}
}


void UExtraGameAnimInstance::OnSprintStateLeft()
{
	bEvadeToSprint = false;
	if (OwnerCharacter)
	{
		OwnerCharacter->SetSprinting(false);
	}
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
		
		Acceleration = OwnerMovementComp->GetCurrentAcceleration().Length();
	}

	//进入空中立刻清理停步
	if (bisFalling || bisJumping)
	{
		ClearStopRequest();
	}

	// 停步请求超时兜底：等 FootPlant 等太久就强制放行，避免标记漏配 / ABP 未进停步状态时
	// 停步请求一直挂着（移动组件持续用停步刹车、这里的 GroundSpeed 也一直冻结）
	if (bRequestStop && GetWorld()
		&& GetWorld()->GetTimeSeconds() - StopRequestTime > StopRequestTimeout)
	{
		ClearStopRequest();
	}

	if (OwnerCharacter && OwnerMovementComp)
	{
		bIsMoving = GroundSpeed > 3.f && OwnerMovementComp->IsMovingOnGround();

		//缓存速度用来处理停步
		if (!bRequestStop)
		{
			CacheVelocity = OwnerCharacter->GetVelocity();
			GroundSpeed = CacheVelocity.Size2D();
		}
		//停步且Sprint
		else if (bEvadeToSprint)
		{
			CacheVelocity = OwnerCharacter->GetVelocity();
			GroundSpeed = CacheVelocity.Size2D();
			bEvadeToSprint = false;
		}
		// 停步窗口：不更新 CacheVelocity / GroundSpeed，让动画侧速度保持跑步值，
		// BlendSpace 与 bIsMoving 维持跑步姿态直到 ABP 进入停步状态。
		// 这里不能再把 Velocity 写回 CacheVelocity（旧做法）：那会绕过 CMC 的刹车、碰撞与地面摩擦，
		// 撞墙/斜坡时两帧反复互相覆盖，表现为抖动与速度残留。
		// 实际的减速由移动组件的 StopBrakingDeceleration 负责（见 SetStopRequest）
	}

	//待机动作更新
	UpdateIdleActionSystem(DeltaSeconds);
}

void UExtraGameAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
}

int32 UExtraGameAnimInstance::PickNextIdleAction()
{
	//当前总待机动画数量
	const int32 N = IdleActionEntries.Num();
	if (N == 0) return 0;
	if (N == 1) return 1;

	//数组内随机找一个和上一个播放待机动画不同的进行更新
	int32 RandomIdx = FMath::RandRange(0, N - 2);
	if (RandomIdx >= PrevIdleActionIndex)
	{
		++RandomIdx;
	}

	PrevIdleActionIndex = RandomIdx;
	
	return RandomIdx + 1;
}

float UExtraGameAnimInstance::CalculateStrideBlend() const
{
	// 步幅曲线：把 GroundSpeed 映射到步幅系数，缩放脚步位移使其匹配移动速度。
	const float CurveTime = GroundSpeed / GetOwningComponent()->GetComponentScale().Z;
	
	const float ClampedGait = GetAnimCurveClamped(NAME_W_Gait, -1.0, 0.0f, 1.0f);
	//获取曲线上对应步幅的值并返回
	const float LerpStrideBlend =
		FMath::Lerp(StrideBlend_N_Walk->GetFloatValue(CurveTime), StrideBlend_N_Run->GetFloatValue(CurveTime),ClampedGait);
	
	return LerpStrideBlend;
}

float UExtraGameAnimInstance::CalculateStandingPlayRate() const
{
	const float LerpedSpeed = FMath::Lerp(GroundSpeed / OwnerCharacter->WalkSpeed,GroundSpeed / OwnerCharacter->RunSpeed,
									  GetAnimCurveClamped(NAME_W_Gait, -1.0f, 0.0f, 1.0f));

	const float SprintAffectedSpeed = FMath::Lerp(LerpedSpeed, GroundSpeed / OwnerCharacter->SprintSpeed,
												  GetAnimCurveClamped(NAME_W_Gait, -2.0f, 0.0f, 1.0f));

	return FMath::Clamp((SprintAffectedSpeed / CalculateStrideBlend()) / GetOwningComponent()->GetComponentScale().Z,0.0f, 3.0f);
}

float UExtraGameAnimInstance::GetAnimCurveClamped(const FName& Name, float Bias, float ClampMin, float ClampMax) const
{
	return FMath::Clamp(GetCurveValue(Name) + Bias, ClampMin, ClampMax);
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
		//默认20s
		const int32 ArrayIdx = IdleActionIndex - 1;
		const float ActionDuration = IdleActionEntries.IsValidIndex(ArrayIdx)
			? IdleActionEntries[ArrayIdx].Duration
			: 20.0f;

		if (IdlePhaseTimer >= ActionDuration)
		{
			IdleActionIndex = 0;
			IdlePhaseTimer = 0.f;
			CurrentIdleActionSequence = nullptr;
		}
	}
}
