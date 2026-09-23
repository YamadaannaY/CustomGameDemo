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
static TAutoConsoleVariable<int32> CVarCombatCameraDebugLog(
	TEXT("CombatCamera.Debug.Log"),
	0,
	TEXT("1 = 打印战斗相机接管 / 交还的关键朝向（排查镜头跳变用）"));

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
		CVarCombatCameraDebugLog.AsVariable()->Set(0, ECVF_SetByConsole);
	}));

UCombatCameraComponent::UCombatCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

}

void UCombatCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	
	CacheCameraComponents();

	// 从实际 SpringArm/Camera 读取初始值
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
		
		//更新Req，直接替换栈顶部
		ActiveReq = &DebugOverride;
		ActiveReqId = -1;
	}

	// 目标状态：有请求 → 逐项按「是否修改」门控取值；无请求 → 全部淡出回基准值。
	// 未勾选「是否修改」的项不是「回归基准」，而是「本镜头不干预」：目标取当前值，等效于冻结不动。
	// 相机位置偏移只有 Y / Z 两个分量，X 恒为 0。
	FVector TargetLoc = FVector::ZeroVector;
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

		// 镜头重叠切换：新镜头用 BlendInTime 进场，被它顶下去的旧镜头用 BlendOutTime 退场，二者取较长者
		if (PrevActiveRequestId != INDEX_NONE && PrevActiveRequestId != ActiveReqId)
		{
			BlendTime = FMath::Max(BlendTime, PrevActiveBlendOutTime);
		}
	}

	// 暂停（PIE pause）+ 调试覆盖时，世界时间冻结、DeltaTime 为 0，插值会原地不动。
	// 这种情况下直接 snap 到目标，命令一改相机一帧到位，便于逐帧修改相机参数。
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
		const float InterpSpeed = (BlendTime > KINDA_SMALL_NUMBER) ? (1.f / BlendTime) : 10.f;
		
		//Vector
		CurrentLocationOffset = FMath::VInterpTo(CurrentLocationOffset, TargetLoc, DeltaTime, InterpSpeed);
		//Rotator
		CurrentArmRotationOffset = FMath::RInterpTo(CurrentArmRotationOffset, TargetArmRot, DeltaTime, InterpSpeed);
		//Float
		CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetArm, DeltaTime, InterpSpeed);
		CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, InterpSpeed);
	}

	FollowCamera->SetRelativeLocation(CurrentLocationOffset);
	FollowCamera->SetFieldOfView(CurrentFOV);

	// 臂朝向接管：对齐角色朝向 / 锁 look / 臂旋转偏移
	UpdateBoomRotation(DeltaTime, ActiveReq, ActiveReqId);

	// ArmLength 与 Zoom 共享 SpringArm->TargetArmLength，仅在有请求或尚未淡出回基准值时接管
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
	LogDebugState(TEXT("Push"));
	return Id;
}

void UCombatCameraComponent::PopRequest(int32 RequestId)
{
	if (const FCombatCameraRequest* Req = ActiveRequests.Find(RequestId))
	{
		PendingBlendOutTime = Req->BlendOutTime;
	}
	ActiveRequests.Remove(RequestId);

	// 打出被移除的 id 与剩余数量
	// 以及窗口重叠 / 提前被 ClearAllRequests 收走的情况
	if (CVarCombatCameraDebugLog.GetValueOnGameThread() != 0)
	{
		LogDebugState(*FString::Printf(TEXT("Pop(id=%d,left=%d)"), RequestId, ActiveRequests.Num()));
	}

	// 被移除的正是接管来源 → 清掉接管标记，剩下的请求由下一帧 Tick 重新评估
	if (HijackRequestId == RequestId)
	{
		EndRotationHijack();
	}
}

void UCombatCameraComponent::ClearAllRequests()
{
	// 打断淡出用独立时长：镜头被异常掐断（GA Cancel）与正常播完的收尾节奏通常不同,更短
	if (const FCombatCameraRequest* ActiveReq = FindActiveRequest())
	{
		PendingBlendOutTime = ActiveReq->InterruptedBlendOutTime;
	}

	ActiveRequests.Empty();
	LogDebugState(TEXT("ClearAll"));
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
	const bool bArmOffsetHeld = IsArmOffsetHeld();

	//接管已经结束，重置Id
	if (!bWantHijack)
	{
		if (HijackRequestId != INDEX_NONE)
		{
			EndRotationHijack();
		}
	}
	//接管
	else if (HijackRequestId != ActiveReqId)
	{
		// 接管来源换了（更高优先级的镜头接管，或首个接管镜头进入）→ 重起过渡
		BeginRotationHijack(*ActiveReq, ActiveReqId);
	}

	// 接管在整个请求窗口内都持有臂朝向：FacingBasis 要逐帧跟随角色转身，LockLook 要一直钉住
	// 进入时的视角，即窗口内持续锁定Boom旋转
	FRotator BoomBase = FRotator::ZeroRotator;
	if (bWantHijack)
	{
		// 角色朝向基准 → 目标随角色转身逐帧变化；否则目标固定（进入瞬间的视角）
		FRotator Target = bHijackUseFacingBasis
			? GetCharacterFacingRotation(bHijackFrontFacing) //是否开启前向180旋转
			: HijackFrozenRotation;

		// 「角色朝向基准 + 允许 look」时把玩家进入接管后转过的角度叠在目标上：否则这几秒过渡里转视角毫无反应，过渡结束还会被一次性抹掉。
		if (bHijackUseFacingBasis && !bHijackLockLook)
		{
			if (const APlayerController* PC = GetOwningPlayerController())
			{
				Target = (Target + (PC->GetControlRotation() - HijackEnterControlRotation).GetNormalized()).GetNormalized();
			}
		}

		if (HijackPhase == EHijackPhase::Holding)
		{
			// 已就位：目标随角色逐帧变化，用平滑跟随
			// 起点取「上次写出去的值」而不是组件旋转，否则角色转身角度也会参与跟随位置计算
			const FRotator CurrentBoom = GetLastBoomBaseRotation();
			const float FollowSpeed = (HijackBlendTime > KINDA_SMALL_NUMBER) ? (1.f / HijackBlendTime) : 10.f;
			BoomBase = FMath::RInterpTo(CurrentBoom, Target, DeltaTime, FollowSpeed);
		}
		else
		{
			// 进入时的过渡用 Slerp + 显式进度而非指数插值：TransitionTime 内一定转完
			HijackElapsed += DeltaTime;
			const float Alpha = (HijackBlendTime > KINDA_SMALL_NUMBER)
				? FMath::Clamp(HijackElapsed / HijackBlendTime, 0.f, 1.f)
				: 1.f;
			BoomBase = FQuat::Slerp(HijackStartRotation.Quaternion(), Target.Quaternion(), Alpha).Rotator();

			if (Alpha >= 1.f)
			{
				// 切到 Holding：之后改用平滑跟随去追随角色变化的目标，不再重跑这段 Slerp 过渡
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

	if (bWantHijack || bArmOffsetHeld)
	{
		LastBoomWorldRotation = (BoomBase + CurrentArmRotationOffset).GetNormalized();
		CameraBoom->bUsePawnControlRotation = false;
		CameraBoom->SetWorldRotation(LastBoomWorldRotation);
		bBoomOwned = true;
	}
	else
	{
		if (bBoomOwned)
		{
			LogDebugState(TEXT("Release-Before"));

			// 交还写入权：把上一次真正写出去的朝向还原成「基准」后折进 ControlRotation。
			// 不能读组件旋转——它是相对父级的，角色在两次写入之间转动会让它漂移（没有勾选Lock的情况下）；
			// 也不能直接写合成朝向——那会让臂旋转偏移叠加两次（见 GetLastBoomBaseRotation）。
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->SetControlRotation(GetLastBoomBaseRotation());
			}

			// 本帧也要把臂压回去：从这一帧起本组件不再写它，而臂的旋转是相对父级的，
			// 角色本帧的转动会直接漏进画面（表现为交还瞬间闪一下）。SpringArm 下一帧会用
			// ControlRotation（同一个值）覆盖，所以这次写入只是补上了这一帧。
			CameraBoom->SetWorldRotation(LastBoomWorldRotation);

			bBoomOwned = false;

			LogDebugState(TEXT("Release-After"));
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

	//  过渡起点、以及「玩家 look 基准」的选取：
	//  臂正由本组件写（bBoomOwned）→ 用本组件最后写出去的那个朝向。此时 ControlRotation 可能是锁
	//   look 期间没被更新的旧方向，不能拿它当起点；也不能读组件旋转（相对父级、会随角色转动漂移）。
	//  臂由 bUsePawnControlRotation 驱动 → 组件旋转可能滞后一帧（取决于 SpringArm 与本组件的 tick
	//  顺序），改用 ControlRotation 更准；不接管时臂偏移必然接近 0，叠上去即可。
	APlayerController* PC = GetOwningPlayerController();
	HijackEnterControlRotation = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;

	if (bBoomOwned)
	{
		// 用本组件最后写出的「基准」朝向：LastBoomWorldRotation 含臂旋转偏移，而 ControlRotation
		// 只存基准（臂上会再叠一次偏移），直接写合成朝向会让偏移叠加两次。
		HijackStartRotation = GetLastBoomBaseRotation();
	}
	else if (!PC)
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

	LogDebugState(TEXT("Hijack-Begin"));
}

void UCombatCameraComponent::EndRotationHijack()
{
	LogDebugState(TEXT("Hijack-End"));

	// 接管一结束就把臂朝向折进 ControlRotation——不能等到「交还写入权」那一步。
	// 臂旋转偏移还在淡出时我们仍然持有臂，而下一帧 BoomBase 会立刻回落到 ControlRotation；
	// 它一直是接管前的旧方向（通常就是角色正后方），镜头会当场跳过去并停在那儿。
	// 写进去的是「基准」（去掉偏移），因为继续持有时臂上还会再叠一次偏移。
	if (bBoomOwned)
	{
		if (APlayerController* PC = GetOwningPlayerController())
		{
			PC->SetControlRotation(GetLastBoomBaseRotation());
		}
	}

	HijackRequestId = INDEX_NONE;
	HijackPhase = EHijackPhase::None;
	HijackElapsed = 0.f;

	// 写入权不在这里交还：臂旋转偏移可能还没淡出完，要继续由 UpdateBoomRotation 握着才不会被顶掉
	// （此时 ControlRotation 已经是对齐后的朝向，所以继续持有也不会跳）
}

void UCombatCameraComponent::LogDebugState(const TCHAR* Tag) const
{
	if (CVarCombatCameraDebugLog.GetValueOnGameThread() == 0)
	{
		return;
	}

	const FCombatCameraRequest* ActiveReq = FindActiveRequest();
	const bool bWantHijack = ActiveReq && (ActiveReq->bUseCharacterFacingBasis || ActiveReq->bLockLookInput);
	const APlayerController* PC = GetOwningPlayerController();
	const AActor* Owner = GetOwner();
	
	// req = 当前生效请求的配置（F/Front/LockLook/ArmRot）；snap = 本次接管进入时锁存的快照。
	// ArmOffY/held = 臂旋转偏移的 Yaw 与「是否仍在持有臂」，接管结束后镜头跳回旧方向就出在这上面。
	UE_LOG(LogTemp, Warning,
		TEXT("[CombatCam] %-15s owned=%d want=%d phase=%d id=%d | req F%d/%d/L%d/A%d snap F%d/%d/L%d | Last=%.1f Comp=%.1f Ctrl=%.1f Owner=%.1f Arm=%.0f ArmOffY=%.1f held=%d"),
		Tag,
		bBoomOwned ? 1 : 0,
		bWantHijack ? 1 : 0,
		static_cast<int32>(HijackPhase),
		HijackRequestId,
		(ActiveReq && ActiveReq->bUseCharacterFacingBasis) ? 1 : 0,
		(ActiveReq && ActiveReq->bFrontFacingBasis) ? 1 : 0,
		(ActiveReq && ActiveReq->bLockLookInput) ? 1 : 0,
		(ActiveReq && ActiveReq->bModifyArmRotation) ? 1 : 0,
		bHijackUseFacingBasis ? 1 : 0, bHijackFrontFacing ? 1 : 0, bHijackLockLook ? 1 : 0,
		LastBoomWorldRotation.Yaw,
		CameraBoom ? CameraBoom->GetComponentRotation().Yaw : 0.f,
		PC ? PC->GetControlRotation().Yaw : 0.f,
		Owner ? Owner->GetActorRotation().Yaw : 0.f,
		CurrentArmLength,
		CurrentArmRotationOffset.Yaw,
		IsArmOffsetHeld() ? 1 : 0);
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
	if (const AExtraPlayerCharacter* Owner = Cast<AExtraPlayerCharacter>(GetOwner()))
	{
		CameraBoom = Owner->GetCameraBoom();
		FollowCamera = Owner->GetFollowCamera();
	}
}