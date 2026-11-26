#include "ExtraGameMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"

namespace
{
	//将Value从其在In区间的比例值转换为Out区间的比例值，具体用于将速度进行区间映射，落在Out区间内，用于匹配曲线
	float MapRangeClamped(float Value, float InMin, float InMax, float OutMin, float OutMax)
	{
		if (InMax <= InMin)
		{
			return OutMin;
		}

		const float T = FMath::Clamp((Value - InMin) / (InMax - InMin), 0.f, 1.f);
		return FMath::Lerp(OutMin, OutMax, T);
	}
}

UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 用自定义 MoveData 容器替换默认容器，使 ServerMove 包携带客户端朝向
	SetNetworkMoveDataContainer(CustomNetworkMoveDataContainer);
}

void UExtraGameMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// 缓存角色朝向和摄像机朝向初值
	
	if (CharacterOwner)
	{
		TargetRotation = FRotator(0.f, CharacterOwner->GetActorRotation().Yaw, 0.f);
		LastRotationTarget = TargetRotation;
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
	const float Speed = Velocity.Size2D();

	if (GaitRunSpeed <= GaitWalkSpeed || GaitSprintSpeed <= GaitRunSpeed)
	{
		UE_LOG(LogTemp,Error,TEXT("三档速度配置错误！"));
	}

	/*	在三档速度区间内进行映射	*/

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
	if (!IsMovingOnGround() || !MovementCurve)
	{
		return Super::GetMaxAcceleration();
	}
	
	return MovementCurve->GetVectorValue(GetMappedSpeed()).X;
}

float UExtraGameMovementComponent::GetMaxBrakingDeceleration() const
{
	// 停步窗口内用专用减速度，调节减速趋势
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
		//与当前速度成正比的阻力
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

	// 服务端上的远程客户端：朝向由客户端主导。
	// bSuppressServerRotation 只在本次 MoveData 处理期间为 true。
	if (CharacterOwner->GetLocalRole() == ROLE_Authority
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy
		&& bSuppressServerRotation)
	{
		return;
	}
	
	if (!bUseCustomPhysicsRotation)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	// 判定「有没有 montage 在播」—— Montage存在 MW 接管朝向的情况，而Jogging基本状态没有 montage
	const UAnimInstance* AnimInst = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const bool bAnimDrivingRotation = AnimInst && AnimInst->IsAnyMontagePlaying();

	if (bAnimDrivingRotation)
	{
		TargetRotation = FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
		LastRotationTarget = TargetRotation;
		return;
	}

	// 地面且启用 OrientToMovement：由本项目的双层插值接管地面转向。
	// 其余运动模式（空中 / 游泳 / 飞行）以及 bUseControllerDesiredRotation 分支
	// 全部交回基类，保持原有行为。
	if (IsMovingOnGround() && bOrientRotationToMovement)
	{
		// 「最后有效方向」只在地面转向里使用：无输入时沿用上一次的方向，避免松手瞬间方向乱飘。
		// 记录的是「输入方向」而不是速度方向：反向/急变向时速度要先刹车过零才掉头，方向滞后很多，
		// 短促输入松开后速度方向还没转到位，角色会停在侧后方。输入方向即时反映玩家意图。
		// 判据只看 Acceleration：它每帧由 ConsumeInputVector() 重算（ControlledCharacterMove），
		if (!Acceleration.IsNearlyZero())
		{
			LastRotationTarget = FRotator(0.f, Acceleration.ToOrientationRotator().Yaw, 0.f);
		}

		CustomPhysicsRotation(DeltaTime);
		return;
	}

	// 空中 / 游泳 / 飞行等其余模式交回基类。空中额外用 AirRotationScale 缩放基类使用的
	// RotationRate.Yaw，保留「空中比地面更难转向」的手感；系数不能为 0，
	// 基类的 RInterpConstantTo 在速率为 0 时会直接对齐目标。
	if (IsFalling())
	{
		TGuardValue<decltype(RotationRate.Yaw)> AirRateGuard(
			RotationRate.Yaw, RotationRate.Yaw * AirRotationScale);
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	Super::PhysicsRotation(DeltaTime);
}

void UExtraGameMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	// 容器不是我们的自定义类型时，走原生逻辑
	if (&GetNetworkMoveDataContainer() != &CustomNetworkMoveDataContainer)
	{
		Super::ServerMove_PerformMovement(MoveData);
		return;
	}

	const FExtraCharacterNetworkMoveData& Extra =
		static_cast<const FExtraCharacterNetworkMoveData&>(MoveData);
	
	//抑制服务端的CustomPhysicsRotation
	TGuardValue<bool> SuppressGuard(bSuppressServerRotation, true);

	// 必须在 Super 之前取上一次的时间戳：Super 内部会把 CurrentClientTimeStamp 推进到本次 move，
	// 之后再算差值恒为 0，而我们需要时间戳差值计算预期的最大Yaw值，避免误差。
	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	//CurrentClientTimeStamp == 记录的最近一次处理的移动包时间戳
	const float PrevClientTimeStamp = ServerData ? ServerData->CurrentClientTimeStamp : 0.f;

	// 原生逻辑处理移动
	Super::ServerMove_PerformMovement(MoveData);

	// 这一部分处理旋转
	if (Extra.bHasClientRotation && CharacterOwner && ServerData &&
		ServerData->CurrentClientTimeStamp == MoveData.TimeStamp)
	{
		FRotator DesiredRotation = Extra.ClientRotation;

		// 基准取服务端当前朝向和Delta值
		const double CurrentYaw = CharacterOwner->GetActorRotation().Yaw;
		const double DeltaYaw   = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredRotation.Yaw);

		// 根运动期间朝向由动画驱动，单帧步进可能很大，直接放行
		const bool bAllowLargeStep = HasAnimRootMotion()
			|| CharacterOwner->GetRootMotionAnimMontageInstance() != nullptr;

		float Dt     = 0.f;
		float Budget = 0.f;
		bool  bClamped = false;
		
		//基于客户端上传值算出的
		const float ClientDt = MoveData.TimeStamp - PrevClientTimeStamp;

		if (!bAllowLargeStep && MaxYawRateForValidation > 0.f && ClientDt > 0.f)
		{
			Dt = ClientDt;

			// 本帧允许的角度预算：按本次 move 实际覆盖的时长算，不用固定每包，否则丢包/合包会误判
			Budget = MaxYawRateForValidation * Dt * (1.f + YawValidationTolerance);

			if (FMath::Abs(DeltaYaw) > Budget)
			{
				bClamped = true;
				DesiredRotation.Yaw = CurrentYaw + FMath::Sign(DeltaYaw) * Budget;
			}
		}

		if (bClamped)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Rotation] 客户端朝向超出单帧预算 %.1f > %.1f 度 (dt=%.4f)，已钳制到预算内"),
				(float)FMath::Abs(DeltaYaw), Budget, Dt);
		}

		//只接受 Yaw：Pitch/Roll 归零
		DesiredRotation.Pitch = 0.f;
		DesiredRotation.Roll  = 0.f;

		MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
	}
}

void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	// 相机转速更新（供速率放大用）
	CachedCameraYawRate = 0.f;
	if (const AController* OwnerController = CharacterOwner->GetController())
	{
		//模拟代理上没有Controller,
		const float CurrentCameraYaw = OwnerController->GetControlRotation().Yaw;
		CachedCameraYawRate = FMath::Abs(
			FMath::FindDeltaAngleDegrees(LastCameraYaw, CurrentCameraYaw)
			/ FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		LastCameraYaw = CurrentCameraYaw;
	}

	const float Speed = Velocity.Size2D();
	const bool bHasMovementInput = !Acceleration.IsNearlyZero();

	// 有输入就转（含原地转身）；无输入但仍有惯性时继续收敛。
	const bool bShouldRotate = bHasMovementInput || Speed > FallbackRotationSpeed;

	const float GroundedRate = CalculateGroundedRotationRate();
	
	//  有输入就转；已松手但速度还快时也允许回正
	if (!bShouldRotate)
	{
		return;
	}

	// 本函数只在地面分支被调用；空中转向已交回基类 PhysicsRotation。
	SmoothCharacterRotation(LastRotationTarget, TargetRotationInterpSpeed, GroundedRate, DeltaTime);
}

void UExtraGameMovementComponent::SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed,
                                                           float ActorInterpSpeed, float DeltaTime)
{
	// 第一层：中间目标匀速逼近外部目标，起手/变向都不会有硬拐点
	TargetRotation = FMath::RInterpConstantTo(TargetRotation, Target, DeltaTime, TargetInterpSpeed);

	// 第二层：角色指数逼近中间目标
	FRotator DesiredRotation = FMath::RInterpTo(
		UpdatedComponent->GetComponentRotation(), TargetRotation, DeltaTime, ActorInterpSpeed);
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}

float UExtraGameMovementComponent::CalculateGroundedRotationRate() const
{
	float BaseRate = RotationRate.Yaw;
	if (RotationRateCurve)
	{
		const float CurveValue = RotationRateCurve->GetFloatValue(GetMappedSpeed());
		if (CurveValue > 0.f)
		{
			BaseRate = CurveValue;
		}
	}

	float Rate = BaseRate;

	// 相机甩得越快，角色跟得越快
	if (MaxCameraYawRate > 0.f)
	{
		const float CameraYawRateScale = MapRangeClamped(
			GetCameraYawRate(), 0.f, MaxCameraYawRate, 1.f, FMath::Max(MaxCameraYawRateMultiplier, 1.f));
		Rate *= CameraYawRateScale;
	}
	
	//Clamp
	if (MaxGroundedRotationRate > 0.f)
	{
		Rate = FMath::Min(Rate, MaxGroundedRotationRate);
	}

	return Rate;
}
