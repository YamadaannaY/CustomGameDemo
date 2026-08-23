#include "UCombatCameraComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"

// ── 调试控制台变量：PIE 里实时覆盖战斗相机参数，找到满意值后抄回 ANS ──
static TAutoConsoleVariable<int32> CVarCombatCameraDebugOverride(
	TEXT("CombatCamera.Debug.Override"),
	0,
	TEXT("1 = 用下方控制台参数覆盖战斗相机目标（忽略 Montage 请求），便于实时调参"));

static TAutoConsoleVariable<float> CVarCombatCameraDebugLocX(TEXT("CombatCamera.Debug.LocX"), 0.f, TEXT("调试位置偏移 X"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugLocY(TEXT("CombatCamera.Debug.LocY"), 0.f, TEXT("调试位置偏移 Y"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugLocZ(TEXT("CombatCamera.Debug.LocZ"), 0.f, TEXT("调试位置偏移 Z"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugPitch(TEXT("CombatCamera.Debug.Pitch"), 0.f, TEXT("调试旋转偏移 Pitch"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugYaw(TEXT("CombatCamera.Debug.Yaw"), 0.f, TEXT("调试旋转偏移 Yaw"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugRoll(TEXT("CombatCamera.Debug.Roll"), 0.f, TEXT("调试旋转偏移 Roll"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugArmLength(TEXT("CombatCamera.Debug.ArmLength"), 300.f, TEXT("调试相机臂长度"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugFOV(TEXT("CombatCamera.Debug.FOV"), 90.f, TEXT("调试 FOV"));

// 一键重置所有调试 CVar 到默认值：TAutoConsoleVariable 是静态变量，编辑器进程存活期间
// PIE 里改的值会残留到下次 PIE，用这个命令手动清回默认（彻底重置需关闭编辑器重启进程）。
static FAutoConsoleCommand CmdCombatCameraDebugReset(
	TEXT("CombatCamera.Debug.Reset"),
	TEXT("重置所有战斗相机调试 CVar 到默认值"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		CVarCombatCameraDebugOverride.AsVariable()->Set(0, ECVF_SetByConsole);
		CVarCombatCameraDebugLocX.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugLocY.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugLocZ.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugPitch.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugYaw.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugRoll.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugArmLength.AsVariable()->Set(300.f, ECVF_SetByConsole);
		CVarCombatCameraDebugFOV.AsVariable()->Set(90.f, ECVF_SetByConsole);
	}));

UCombatCameraComponent::UCombatCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// 暂停（PIE pause）时仍 Tick，便于暂停下调参实时定位相机。
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UCombatCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheCameraComponents();

	// 从实际 SpringArm/Camera 读取初始值，而非硬编码 300/90：
	// 否则 override 开启瞬间会把臂长/FOV 拉到硬编码值，与当前实际值不符，产生跳变。
	if (CameraBoom)
	{
		BaseArmLength = CameraBoom->TargetArmLength;
		CurrentArmLength = BaseArmLength;
	}
	if (FollowCamera)
	{
		BaseFOV = FollowCamera->FieldOfView;
		CurrentFOV = BaseFOV;
	}
	bBaseCached = true;
}

void UCombatCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	const FCombatCameraRequest* ActiveReq = FindActiveRequest();

	// 调试 override：控制台开启后，用 CVar 参数覆盖目标（忽略 Montage 请求）。
	const bool bDebugOverride = (CVarCombatCameraDebugOverride.GetValueOnGameThread() != 0);
	FCombatCameraRequest DebugOverride;
	if (bDebugOverride)
	{
		DebugOverride.LocationOffset = FVector(
			CVarCombatCameraDebugLocX.GetValueOnGameThread(),
			CVarCombatCameraDebugLocY.GetValueOnGameThread(),
			CVarCombatCameraDebugLocZ.GetValueOnGameThread());
		DebugOverride.RotationOffset = FRotator(
			CVarCombatCameraDebugPitch.GetValueOnGameThread(),
			CVarCombatCameraDebugYaw.GetValueOnGameThread(),
			CVarCombatCameraDebugRoll.GetValueOnGameThread());
		DebugOverride.ArmLength = CVarCombatCameraDebugArmLength.GetValueOnGameThread();
		DebugOverride.FOV = CVarCombatCameraDebugFOV.GetValueOnGameThread();
		DebugOverride.BlendInTime = 0.05f;
		DebugOverride.BlendOutTime = 0.2f;
		ActiveReq = &DebugOverride;
	}

	// 目标状态：有请求 → 请求参数；无请求 → 淡出回基准值。
	FVector TargetLoc = FVector::ZeroVector;
	FRotator TargetRot = FRotator::ZeroRotator;
	float TargetArm = BaseArmLength;
	float TargetFOV = BaseFOV;
	float BlendTime = PendingBlendOutTime;

	if (ActiveReq)
	{
		TargetLoc = ActiveReq->LocationOffset;
		TargetRot = ActiveReq->RotationOffset;
		TargetArm = ActiveReq->ArmLength;
		TargetFOV = ActiveReq->FOV;
		BlendTime = ActiveReq->BlendInTime;
	}

	// 暂停（PIE pause）+ 调试覆盖时，世界时间冻结、DeltaTime 为 0，插值会原地不动。
	// 这种情况下直接 snap 到目标，命令一改相机立刻到位，便于逐帧定位。
	const bool bPaused = GetWorld() && GetWorld()->IsPaused();
	if (bPaused && bDebugOverride)
	{
		CurrentLocationOffset = TargetLoc;
		CurrentRotationOffset = TargetRot;
		CurrentArmLength = TargetArm;
		CurrentFOV = TargetFOV;
	}
	else
	{
		// FInterpTo 用「1/时间」作为速度，BlendTime 越小趋近越快。
		const float InterpSpeed = (BlendTime > KINDA_SMALL_NUMBER) ? (1.f / BlendTime) : 1000.f;

		CurrentLocationOffset = FMath::VInterpTo(CurrentLocationOffset, TargetLoc, DeltaTime, InterpSpeed);
		CurrentRotationOffset = FMath::RInterpTo(CurrentRotationOffset, TargetRot, DeltaTime, InterpSpeed);
		CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetArm, DeltaTime, InterpSpeed);
		CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, InterpSpeed);
	}

	// 位置/旋转叠加在 ViewCam 上（相对 SpringArm 末端，不被 bUsePawnControlRotation 覆盖）。
	FollowCamera->SetRelativeLocation(CurrentLocationOffset);
	FollowCamera->SetRelativeRotation(CurrentRotationOffset);
	FollowCamera->SetFieldOfView(CurrentFOV);

	// ArmLength 与 Zoom 共享 SpringArm->TargetArmLength，仅在有请求或尚未淡出回基准值时接管；
	// 到达基准值后停止写入，把 ArmLength 交还给 Zoom 逻辑。
	const bool bShouldManageArm = (ActiveReq != nullptr)
		|| (FMath::Abs(CurrentArmLength - BaseArmLength) > 1.f);

	if (bShouldManageArm)
	{
		CameraBoom->TargetArmLength = CurrentArmLength;
	}
}

int32 UCombatCameraComponent::PushRequest(const FCombatCameraRequest& Request)
{
	CacheBaseIfNeeded();

	const int32 Id = NextRequestId++;
	ActiveRequests.Add(Id, Request);
	return Id;
}

void UCombatCameraComponent::PopRequest(int32 RequestId)
{
	if (const FCombatCameraRequest* Req = ActiveRequests.Find(RequestId))
	{
		PendingBlendOutTime = Req->BlendOutTime;
	}
	ActiveRequests.Remove(RequestId);
}

void UCombatCameraComponent::ClearAllRequests()
{
	ActiveRequests.Empty();
	PendingBlendOutTime = 0.2f;
}

const FCombatCameraRequest* UCombatCameraComponent::FindActiveRequest() const
{
	const FCombatCameraRequest* Result = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Min();
	int32 BestId = TNumericLimits<int32>::Min();

	for (const TPair<int32, FCombatCameraRequest>& Pair : ActiveRequests)
	{
		const FCombatCameraRequest& Req = Pair.Value;
		const bool bHigherPriority = Req.Priority > BestPriority;
		const bool bSamePriorityNewer = (Req.Priority == BestPriority) && (Pair.Key > BestId);
		if (bHigherPriority || bSamePriorityNewer)
		{
			BestPriority = Req.Priority;
			BestId = Pair.Key;
			Result = &Req;
		}
	}

	return Result;
}

void UCombatCameraComponent::CacheBaseIfNeeded()
{
	if (bBaseCached)
	{
		return;
	}

	CacheCameraComponents();

	if (CameraBoom)
	{
		BaseArmLength = CameraBoom->TargetArmLength;
	}
	if (FollowCamera)
	{
		BaseFOV = FollowCamera->FieldOfView;
	}

	bBaseCached = true;
}

void UCombatCameraComponent::CacheCameraComponents()
{
	if (AExtraPlayerCharacter* Owner = Cast<AExtraPlayerCharacter>(GetOwner()))
	{
		CameraBoom = Owner->GetCameraBoom();
		FollowCamera = Owner->GetFollowCamera();
	}
}
