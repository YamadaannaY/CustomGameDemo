#include "UCombatCameraComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"

// ── 调试控制台变量：PIE 里实时覆盖战斗相机参数，找到满意值后抄回 ANS ──
static TAutoConsoleVariable<int32> CVarCombatCameraDebugOverride(
	TEXT("CombatCamera.Debug.Override"),
	0,
	TEXT("1 = 用下方控制台参数覆盖战斗相机目标（忽略 Montage 请求），便于实时调参"));

static TAutoConsoleVariable<float> CVarCombatCameraDebugLocY(TEXT("CombatCamera.Debug.LocY"), 0.f, TEXT("调试相机位置偏移 Y"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugLocZ(TEXT("CombatCamera.Debug.LocZ"), 0.f, TEXT("调试相机位置偏移 Z"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugPitch(TEXT("CombatCamera.Debug.Pitch"), 0.f, TEXT("调试臂旋转偏移 Pitch"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugYaw(TEXT("CombatCamera.Debug.Yaw"), 0.f, TEXT("调试臂旋转偏移 Yaw"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugRoll(TEXT("CombatCamera.Debug.Roll"), 0.f, TEXT("调试臂旋转偏移 Roll"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugArmLength(TEXT("CombatCamera.Debug.ArmLength"), 300.f, TEXT("调试相机臂长度"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugFOV(TEXT("CombatCamera.Debug.FOV"), 90.f, TEXT("调试 FOV"));
static TAutoConsoleVariable<int32> CVarCombatCameraDebugUseCharacterFacing(
	TEXT("CombatCamera.Debug.UseCharacterFacing"),
	0,
	TEXT("1 = 调试 override 模式下偏移基于角色正后方（忽略鼠标旋转）"));
static TAutoConsoleVariable<float> CVarCombatCameraDebugCharacterFacingTransitionTime(
	TEXT("CombatCamera.Debug.CharacterFacingTransitionTime"),
	0.3f,
	TEXT("调试模式切换到角色正后方的过渡时间（秒）"));

// 一键重置所有调试 CVar 到默认值：TAutoConsoleVariable 是静态变量，编辑器进程存活期间
// PIE 里改的值会残留到下次 PIE，用这个命令手动清回默认（彻底重置需关闭编辑器重启进程）。
static FAutoConsoleCommand CmdCombatCameraDebugReset(
	TEXT("CombatCamera.Debug.Reset"),
	TEXT("重置所有战斗相机调试 CVar 到默认值"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		CVarCombatCameraDebugOverride.AsVariable()->Set(0, ECVF_SetByConsole);
		CVarCombatCameraDebugLocY.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugLocZ.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugPitch.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugYaw.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugRoll.AsVariable()->Set(0.f, ECVF_SetByConsole);
		CVarCombatCameraDebugArmLength.AsVariable()->Set(300.f, ECVF_SetByConsole);
		CVarCombatCameraDebugFOV.AsVariable()->Set(90.f, ECVF_SetByConsole);
		CVarCombatCameraDebugUseCharacterFacing.AsVariable()->Set(0, ECVF_SetByConsole);
		CVarCombatCameraDebugCharacterFacingTransitionTime.AsVariable()->Set(0.3f, ECVF_SetByConsole);
	}));

UCombatCameraComponent::UCombatCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	
	// 暂停（PIE pause）时仍 Tick，便于在某一帧暂停下调参实时定位相机。
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UCombatCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	
	CacheCameraComponents();

	// 从实际 SpringArm/Camera 读取初始值，而非硬编码 300/90值，这两个值只是意外读不到初始值时的默认值：
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
}

void UCombatCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CameraBoom || !FollowCamera)
	{
		return;
	}
	
	int32 ActiveReqId = INDEX_NONE;
	const FCombatCameraRequest* ActiveReq = FindActiveRequest(&ActiveReqId);

	// 调试 override：控制台开启后，用 CVar 参数覆盖目标（忽略 Montage 请求）。
	const bool bDebugOverride = (CVarCombatCameraDebugOverride.GetValueOnGameThread() != 0);
	FCombatCameraRequest DebugOverride;
	if (bDebugOverride)
	{
		DebugOverride.bModifyArmLength = true;
		DebugOverride.ArmLength = CVarCombatCameraDebugArmLength.GetValueOnGameThread();

		DebugOverride.bModifyArmRotation = true;
		DebugOverride.ArmRotation = FRotator(
			CVarCombatCameraDebugPitch.GetValueOnGameThread(),
			CVarCombatCameraDebugYaw.GetValueOnGameThread(),
			CVarCombatCameraDebugRoll.GetValueOnGameThread());

		DebugOverride.bModifyCameraOffsetY = true;
		DebugOverride.CameraOffsetY = CVarCombatCameraDebugLocY.GetValueOnGameThread();
		DebugOverride.bModifyCameraOffsetZ = true;
		DebugOverride.CameraOffsetZ = CVarCombatCameraDebugLocZ.GetValueOnGameThread();

		DebugOverride.bModifyFOV = true;
		DebugOverride.FOV = CVarCombatCameraDebugFOV.GetValueOnGameThread();

		DebugOverride.BlendInTime = 0.05f;
		DebugOverride.BlendOutTime = 0.2f;
		DebugOverride.InterruptedBlendOutTime = 0.2f;
		DebugOverride.bUseCharacterFacingBasis = (CVarCombatCameraDebugUseCharacterFacing.GetValueOnGameThread() != 0);
		DebugOverride.CharacterFacingTransitionTime = CVarCombatCameraDebugCharacterFacingTransitionTime.GetValueOnGameThread();
		ActiveReq = &DebugOverride;
		ActiveReqId = -1;
	}

	// 目标状态：有请求 → 逐项按「是否修改」门控取值；无请求 → 全部淡出回基准值。
	// 未勾选「是否修改」的项不是「回归基准」，而是「本镜头不干预」：目标取当前值，等效于冻结不动。
	// 相机偏移的 X 分量不提供修改能力，恒为 0。
	FVector TargetLoc = BaseLocationOffset;
	FRotator TargetArmRot = FRotator::ZeroRotator;
	float TargetArm = BaseArmLength;
	float TargetFOV = BaseFOV;
	float BlendTime = PendingBlendOutTime;

	if (ActiveReq)
	{
		// X 分量无门控，留在基准值上（恒 0）
		TargetLoc.Y = ActiveReq->bModifyCameraOffsetY ? ActiveReq->CameraOffsetY : CurrentLocationOffset.Y;
		TargetLoc.Z = ActiveReq->bModifyCameraOffsetZ ? ActiveReq->CameraOffsetZ : CurrentLocationOffset.Z;
		TargetArmRot = ActiveReq->bModifyArmRotation ? ActiveReq->ArmRotation : CurrentArmRotationOffset;
		TargetArm = ActiveReq->bModifyArmLength ? ActiveReq->ArmLength : CurrentArmLength;
		TargetFOV = ActiveReq->bModifyFOV ? ActiveReq->FOV : CurrentFOV;
		BlendTime = ActiveReq->BlendInTime;

		// 镜头重叠切换：新镜头用 BlendInTime 进场，被它顶下去的旧镜头用 BlendOutTime 退场。
		// 取较长者，否则配在旧镜头上的淡出时间永远不会生效——它只在栈彻底清空时才被读到。
		if (PrevActiveRequestId != INDEX_NONE && PrevActiveRequestId != ActiveReqId)
		{
			BlendTime = FMath::Max(BlendTime, PrevActiveBlendOutTime);
		}
	}

	// 暂停（PIE pause）+ 调试覆盖时，世界时间冻结、DeltaTime 为 0，插值会原地不动。
	// 这种情况下直接 snap 到目标，命令一改相机立刻到位，便于逐帧定位。
	const bool bPaused = GetWorld() && GetWorld()->IsPaused();
	if (bPaused && bDebugOverride)
	{
		CurrentLocationOffset = TargetLoc;
		CurrentArmRotationOffset = TargetArmRot;
		CurrentArmLength = TargetArm;
		CurrentFOV = TargetFOV;
	}
	else
	{
		// FInterpTo 用「1/时间」作为速度，BlendTime 越小趋近越快。NewValue = Current + (Target - Current) * (1 - e^(-InterpSpeed * DeltaTime))
		const float InterpSpeed = (BlendTime > KINDA_SMALL_NUMBER) ? (1.f / BlendTime) : 100.f;

		CurrentLocationOffset = FMath::VInterpTo(CurrentLocationOffset, TargetLoc, DeltaTime, InterpSpeed);
		CurrentArmRotationOffset = FMath::RInterpTo(CurrentArmRotationOffset, TargetArmRot, DeltaTime, InterpSpeed);
		CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetArm, DeltaTime, InterpSpeed);
		CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, InterpSpeed);
	}

	// 相机只承接位置偏移，朝向偏移由臂旋转统一负责（见 UpdateBoomRotation）
	FollowCamera->SetRelativeLocation(CurrentLocationOffset);
	FollowCamera->SetFieldOfView(CurrentFOV);

	// 臂朝向接管：对齐角色朝向 / 锁 look / 臂旋转偏移
	UpdateBoomRotation(DeltaTime, ActiveReq, ActiveReqId);

	// ArmLength 与 Zoom 共享 SpringArm->TargetArmLength，仅在有请求或尚未淡出回基准值时接管；
	// 到达基准值后停止写入，把 ArmLength 交还给 Zoom 逻辑。
	bManagingArmLength = (ActiveReq != nullptr)
		|| (FMath::Abs(CurrentArmLength - BaseArmLength) > 1.f);

	if (bManagingArmLength)
	{
		CameraBoom->TargetArmLength = CurrentArmLength;
	}
	else
	{
		// 不接管时臂长完全归 Zoom：基准跟着它走（而不是停在 BeginPlay 缓存的那个值），
		// 这样战斗结束后回到的是玩家自己调好的距离，CurrentArmLength 也直接对齐、避免追不上。
		BaseArmLength = CameraBoom->TargetArmLength;
		CurrentArmLength = BaseArmLength;
	}

	// 记下本帧生效的请求，供下一帧判断「是不是刚发生镜头切换」并取它的 BlendOutTime
	PrevActiveRequestId = ActiveReq ? ActiveReqId : INDEX_NONE;
	PrevActiveBlendOutTime = ActiveReq ? ActiveReq->BlendOutTime : 0.f;
}

int32 UCombatCameraComponent::PushRequest(const FCombatCameraRequest& Request)
{
	const int32 Id = NextRequestId++;
	ActiveRequests.Add(Id, Request);
	return Id;
}

void UCombatCameraComponent::PopRequest(int32 RequestId)
{
	bool bKeepYaw = false;
	bool bKeepPosition = false;
	bool bResetArmLength = true;

	if (const FCombatCameraRequest* Req = ActiveRequests.Find(RequestId))
	{
		PendingBlendOutTime = Req->BlendOutTime;

		// 仅当退出的正是当前生效请求时才固化机位：低优先级请求退出时屏幕上显示的是别人的状态，
		// 拿它去改写基准会篡改视角。
		if (FindActiveRequest() == Req)
		{
			bKeepYaw = Req->bKeepYawOnEnd;
			bKeepPosition = Req->bKeepCameraPositionOnEnd;
			bResetArmLength = Req->bResetFinalArmLength;
		}
	}
	ActiveRequests.Remove(RequestId);

	// 被移除的正是接管来源 → 清掉接管标记，剩下的请求由下一帧 Tick 重新评估
	if (HijackRequestId == RequestId)
	{
		EndRotationHijack();
	}

	if (bKeepPosition)
	{
		FreezeCameraStateAsBase(bResetArmLength);
	}

	if (bKeepYaw)
	{
		FreezeYawAsBase();
	}
}

void UCombatCameraComponent::ClearAllRequests()
{
	// 打断淡出用独立时长：镜头被异常掐断（GA Cancel）与正常播完的收尾节奏通常不同
	if (const FCombatCameraRequest* ActiveReq = FindActiveRequest())
	{
		PendingBlendOutTime = ActiveReq->InterruptedBlendOutTime;
	}

	ActiveRequests.Empty();
	EndRotationHijack();
}

void UCombatCameraComponent::UpdateBoomRotation(float DeltaTime, const FCombatCameraRequest* ActiveReq, int32 ActiveReqId)
{
	if (!CameraBoom)
	{
		return;
	}

	const bool bWantHijack = (ActiveReq != nullptr)
		&& (ActiveReq->bUseCharacterFacingBasis || ActiveReq->bLockLookInput);

	// 臂旋转偏移没归零前必须一直握着臂的写入权，否则偏移会被 bUsePawnControlRotation 顶掉
	const bool bArmOffsetHeld = !CurrentArmRotationOffset.IsNearlyZero(0.05f);

	if (!bWantHijack)
	{
		if (HijackRequestId != INDEX_NONE)
		{
			EndRotationHijack();
		}
	}
	else if (HijackRequestId != ActiveReqId)
	{
		// 接管来源换了（更高优先级的镜头接管，或首个接管镜头进入）→ 重起过渡
		BeginRotationHijack(*ActiveReq, ActiveReqId);
	}

	// 本帧臂朝向是否由接管给出：
	// 「只对齐一次」的镜头在过渡完成（Holding）后就把基础朝向交回玩家，之后不再干预；
	// 锁 look 的镜头则在 Holding 后长期持有。
	const bool bHijackDrives = bWantHijack
		&& !(HijackPhase == EHijackPhase::Holding && !bHijackLockLook);

	FRotator BoomBase = FRotator::ZeroRotator;
	if (bHijackDrives)
	{
		// 锁 look + 角色朝向基准 → 逐帧跟随角色转身；否则目标固定（进入瞬间的视角或角色朝向）
		FRotator Target = bHijackUseFacingBasis
			? GetCharacterFacingRotation(bHijackFrontFacing)
			: HijackFrozenRotation;

		// 「角色朝向基准 + 允许 look」时把玩家进入接管后转过的角度叠在目标上：
		// 否则这几秒过渡里转视角毫无反应，过渡结束还会被一次性抹掉。
		// 锁 look 与冻结视角两种组合不叠加——它们本来就不该受鼠标影响。
		if (bHijackUseFacingBasis && !bHijackLockLook)
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				Target = (Target + (PC->GetControlRotation() - HijackEnterControlRotation).GetNormalized()).GetNormalized();
			}
		}

		if (HijackPhase == EHijackPhase::Holding)
		{
			// 长期持有阶段（锁 look）：目标随角色转身每帧变化，用平滑跟随而不是直接赋值——
			// 否则角色急转（MW 旋转、根位移）时镜头会硬跟着瞬移。
			const FRotator CurrentBoom = (CameraBoom->GetComponentRotation() - CurrentArmRotationOffset).GetNormalized();
			const float FollowSpeed = (HijackBlendTime > KINDA_SMALL_NUMBER) ? (1.f / HijackBlendTime) : 1000.f;
			BoomBase = FMath::RInterpTo(CurrentBoom, Target, DeltaTime, FollowSpeed);
		}
		else
		{
			// 过渡用 Slerp + 显式进度而非指数插值：TransitionTime 内一定转完，不会因角度大而拖长
			HijackElapsed += DeltaTime;
			const float Alpha = (HijackBlendTime > KINDA_SMALL_NUMBER)
				? FMath::Clamp(HijackElapsed / HijackBlendTime, 0.f, 1.f)
				: 1.f;
			BoomBase = FQuat::Slerp(HijackStartRotation.Quaternion(), Target.Quaternion(), Alpha).Rotator();

			if (Alpha >= 1.f)
			{
				// 保留来源 ID 与 Holding 相位作为「本请求已对齐过」的标记；
				// 若这里清掉 ID，下一帧会被当成新来源重新对齐一次。
				HijackPhase = EHijackPhase::Holding;
			}
		}
	}
	else
	{
		// 基础朝向先取玩家视角：这样臂旋转偏移能叠在它上面，而玩家 look 依然生效
		BoomBase = GetOwningPlayerController()
			? GetOwningPlayerController()->GetControlRotation()
			: CameraBoom->GetComponentRotation();
	}

	if (bHijackDrives || bArmOffsetHeld)
	{
		CameraBoom->bUsePawnControlRotation = false;
		CameraBoom->SetWorldRotation(BoomBase + CurrentArmRotationOffset);
		bBoomOwned = true;
	}
	else
	{
		if (bBoomOwned)
		{
			// 交还写入权：把当前合成朝向折进 ControlRotation，玩家视角才不会跳回进入接管前的旧朝向
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->SetControlRotation(CameraBoom->GetComponentRotation());
			}
			bBoomOwned = false;
		}
		CameraBoom->bUsePawnControlRotation = true;
	}
}

void UCombatCameraComponent::BeginRotationHijack(const FCombatCameraRequest& Request, int32 RequestId)
{
	HijackRequestId = RequestId;
	HijackPhase = EHijackPhase::Blending;
	HijackElapsed = 0.f;

	bHijackUseFacingBasis = Request.bUseCharacterFacingBasis;
	bHijackFrontFacing = Request.bFrontFacingBasis;
	bHijackLockLook = Request.bLockLookInput;

	// 过渡起点、以及「玩家 look 基准」的选取：
	//   臂正由本组件写（bBoomOwned）→ 组件旋转就是玩家看到的朝向，直接用它，且此时 ControlRotation
	//     可能是锁 look 期间没被更新的旧方向，绝不能拿它当起点（会和画面差很远，一切换就跳）。
	//   臂由 bUsePawnControlRotation 驱动 → 组件旋转可能滞后一帧（取决于 SpringArm 与本组件的 tick
	//     顺序），改用 ControlRotation 更准；不接管时臂偏移必然接近 0，叠上去即可。
	APlayerController* PC = GetOwningPlayerController();
	HijackEnterControlRotation = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;

	if (bBoomOwned || !PC)
	{
		HijackStartRotation = CameraBoom ? CameraBoom->GetComponentRotation() : FRotator::ZeroRotator;
	}
	else
	{
		HijackStartRotation = (HijackEnterControlRotation + CurrentArmRotationOffset).GetNormalized();
	}

	if (bHijackUseFacingBasis)
	{
		HijackBlendTime = Request.CharacterFacingTransitionTime;
	}
	else
	{
		// 仅锁 look：目标就是进入瞬间的视角（玩家看到的是臂，所以取合成朝向）
		HijackBlendTime = Request.BlendInTime;
		HijackFrozenRotation = HijackStartRotation;
	}
}

void UCombatCameraComponent::EndRotationHijack()
{
	HijackRequestId = INDEX_NONE;
	HijackPhase = EHijackPhase::None;
	HijackElapsed = 0.f;

	// 写入权不在这里交还：臂旋转偏移可能还没淡出完，要继续由 UpdateBoomRotation 握着才不会被顶掉
}

FRotator UCombatCameraComponent::GetCharacterFacingRotation(bool bFrontFacing) const
{
	FRotator Result = GetOwner() ? GetOwner()->GetActorRotation() : FRotator::ZeroRotator;
	if (bFrontFacing)
	{
		Result.Yaw += 180.f;
		Result.Normalize();
	}
	Result.Pitch = 0.f;
	Result.Roll = 0.f;
	return Result;
}

APlayerController* UCombatCameraComponent::GetOwningPlayerController() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Cast<APlayerController>(Owner->GetInstigatorController()) : nullptr;
}

void UCombatCameraComponent::FreezeCameraStateAsBase(bool bResetArmLength)
{
	// 保留位置：把当前偏移并作新基准。相机本来就停在这一点上，不需要过渡；
	// 之后任何请求出栈都是朝这个新基准淡出，而不是回到进入本镜头前的旧位置。
	BaseLocationOffset = CurrentLocationOffset;

	if (bResetArmLength)
	{
		return;
	}

	// 保留臂长：只把新基准设在夹紧后的最终长度上，不动 CurrentArmLength——
	// 若最终长度超出 Zoom 的上下限，让下面既有的淡出让相机平滑收到边界，而不是原地跳一下。
	// 臂长本来就由本组件负责落到最终值，这里不需要（也不应该）直接写 SpringArm。
	float FinalArmLength = CurrentArmLength;
	if (AExtraPlayerCharacter* Char = Cast<AExtraPlayerCharacter>(GetOwner()))
	{
		FinalArmLength = Char->ApplyArmLengthFromCombatCamera(CurrentArmLength);
	}
	BaseArmLength = FinalArmLength;
}

void UCombatCameraComponent::FreezeYawAsBase()
{
	if (!FollowCamera)
	{
		return;
	}

	// 只保留 Yaw：把相机当前世界朝向的 Yaw 并进玩家视角。因为相机朝向就等于臂朝向（相机层不再
	// 承担旋转偏移），臂旋转偏移带来的 Yaw 会在合成里被抵消，水平方向不跳变；
	// Pitch 不保留，随臂旋转偏移一起淡出回归。
	if (APlayerController* PC = GetOwningPlayerController())
	{
		FRotator NewControlRotation = PC->GetControlRotation();
		NewControlRotation.Yaw = FollowCamera->GetComponentRotation().Yaw;
		PC->SetControlRotation(NewControlRotation);
	}
}

const FCombatCameraRequest* UCombatCameraComponent::FindActiveRequest(int32* OutRequestId) const
{
	const FCombatCameraRequest* Result = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Min();
	int32 BestId = TNumericLimits<int32>::Min();

	//遍历找到最高优先级且最近加入的那一个镜头并调出栈
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

	if (OutRequestId)
	{
		*OutRequestId = Result ? BestId : INDEX_NONE;
	}

	return Result;
}

void UCombatCameraComponent::CacheCameraComponents()
{
	if (AExtraPlayerCharacter* Owner = Cast<AExtraPlayerCharacter>(GetOwner()))
	{
		CameraBoom = Owner->GetCameraBoom();
		FollowCamera = Owner->GetFollowCamera();
	}
}