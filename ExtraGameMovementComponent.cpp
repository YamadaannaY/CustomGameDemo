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

	// 已松手、但速度仍高于此值时也继续回正朝向
	constexpr float FallbackRotationSpeed = 10.f;
}

UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UExtraGameMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// 缓存角色朝向和摄像机朝向初值，否则第一帧会从零朝向跳变
	
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
	
	// 服务器上本地控制的角色（autonomous proxy）朝向由客户端主导：
	// 客户端拥有即时的输入与相机数据，服务器只有经过网络延迟的副本，所以直接用客户端RPC发来的值
	if (CharacterOwner->GetLocalRole() == ROLE_Authority
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy
		&& bHasClientAuthoritativeYaw)
	{
		const FRotator Desired(0.f, ClientAuthoritativeYaw, 0.f);

		// Unreliable 上报间隔不均时直接硬设会抖，用远快于上报周期的速率平滑收口
		const FRotator Smoothed = FMath::RInterpConstantTo(
			UpdatedComponent->GetComponentRotation(), Desired, DeltaTime, ServerYawFollowRate);

		TargetRotation = FRotator(0.f, Smoothed.Yaw, 0.f);
		LastRotationTarget = Desired;
		MoveUpdatedComponent(FVector::ZeroVector, TargetRotation, false);
		return;
	}
	
	// 判定「有没有 montage 在播」—— Montage存在 MW 接管朝向的情况，而Jogging基本状态没有 montage
	const UAnimInstance* AnimInst = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const bool bAnimDrivingRotation = AnimInst && AnimInst->IsAnyMontagePlaying();

	if (bAnimDrivingRotation)
	{
		TargetRotation = FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
		return;
	}

	// 「最后有效方向」是各分支共用的缓存：无输入时沿用上一次的方向，避免松手瞬间方向乱飘。
	// 记录的是「输入方向」而不是速度方向：反向/急变向时速度要先刹车过零才掉头，方向滞后很多，
	// 短促输入松开后速度方向还没转到位，角色会停在侧后方。输入方向即时反映玩家意图。
	// 用 CMC 的 Velocity 而不是 UpdatedComponent->GetComponentVelocity() 做「在动」的判据：
	// 后者靠 UpdateComponentVelocity() 同步，时机不保证，可能整帧读到 0
	if (Velocity.Size2D() > MovingSpeedRefreshThreshold)
	{
		if (!Acceleration.IsNearlyZero(0.001f))
		{
			LastRotationTarget = FRotator(0.f, Acceleration.ToOrientationRotator().Yaw, 0.f);
		}
	}

	CustomPhysicsRotation(DeltaTime);
}

void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	// 相机转速更新（供速率放大用）
	CachedCameraYawRate = 0.f;
	if (const AController* OwnerController = CharacterOwner->GetController())
	{
		//模拟代理上没有Controller是
		const float CurrentCameraYaw = OwnerController->GetControlRotation().Yaw;
		CachedCameraYawRate = FMath::Abs(
			FMath::FindDeltaAngleDegrees(LastCameraYaw, CurrentCameraYaw)
			/ FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		LastCameraYaw = CurrentCameraYaw;
	}

	const float Speed = Velocity.Size2D();
	const bool bIsMoving = Speed > MovingSpeedRefreshThreshold;
	const bool bHasMovementInput = !Acceleration.IsNearlyZero(0.001f);
	const bool bShouldRotate = (bIsMoving && bHasMovementInput) || Speed > FallbackRotationSpeed;

	const float GroundedRate = CalculateGroundedRotationRate();

	if (bDebugRotation && CharacterOwner && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			static_cast<int32>(CharacterOwner->GetUniqueID()), 0.f, FColor::Yellow,
			FString::Printf(
				TEXT("[Rot] Spd=%.0f Accel=%.0f Rotate=%d Rate=%.1f CurveVal=%.1f Mapped=%.2f TargetYaw=%.1f ActorYaw=%.1f LastInputYaw=%.1f"),
				Speed, Acceleration.Size(), bShouldRotate ? 1 : 0, GroundedRate,
				RotationRateCurve ? RotationRateCurve->GetFloatValue(GetMappedSpeed()) : -1.f,
				GetMappedSpeed(), TargetRotation.Yaw, CharacterOwner->GetActorRotation().Yaw,
				LastRotationTarget.Yaw));
	}

	//  要有速度且仍有输入才转向；已松手但速度还快时也允许回正
	if (!bShouldRotate)
	{
		return;
	}

	// 空中削弱作用在两层插值速率上，AirRotationScale=1 时与地面完全等价。
	// 注意不能把系数压到 0：RInterpTo / RInterpConstantTo 在速率为 0 时会直接返回目标（瞬间对齐）
	const float AirScale = IsFalling() ? AirRotationScale : 1.f;

	SmoothCharacterRotation(LastRotationTarget, TargetRotationInterpSpeed * AirScale, GroundedRate * AirScale, DeltaTime);
}

void UExtraGameMovementComponent::SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed,
                                                           float ActorInterpSpeed, float DeltaTime)
{
	// 第一层：中间目标匀速逼近外部目标，起手/变向都不会有硬拐点(使用双层插值的原因，默认的OrientTo直接将加速度方向作为最终方向，没有逼近过程)
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

	if (MaxCameraYawRate <= 0.f)
	{
		return BaseRate;
	}

	// 相机甩得越快，角色跟得越快
	const float CameraYawRateScale = MapRangeClamped(
		GetCameraYawRate(), 0.f, MaxCameraYawRate, 1.f, FMath::Max(MaxCameraYawRateMultiplier, 1.f));

	return BaseRate * CameraYawRateScale;
}
