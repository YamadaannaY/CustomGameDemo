#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UCombatCameraComponent.generated.h"

class USpringArmComponent;
class UCameraComponent;
class APlayerController;

/**
 * 战斗相机Request：动画 Notify State（ANS_CombatCamera）在 Montage 期间提交的相机请求
 * 
 * 这是一个栈结构，具有Push和Pop功能，激活最近Push且优先级最高的那一个Request（eg：优先级相同的镜头重叠时，执行后一个镜头）
 *
 * 请求全部出栈后，所有项统一淡出回无战斗相机时的基准值
 *
 */
USTRUCT(BlueprintType)
struct FCombatCameraRequest
{
	GENERATED_BODY()

	// 淡入时间（秒）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float BlendInTime = 0.2f;

	// 淡出时间（秒）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float BlendOutTime = 0.3f;

	// 打断淡出时间（秒）：镜头被异常掐断（GA Cancel、调用ClearAllRequests 兜底）的淡出时长。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float InterruptedBlendOutTime = 0.2f;

	// 相机臂长（cm）。接管期间覆盖 Zoom 的 TargetArmLength
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bModifyArmLength = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera", meta = (EditCondition = "bModifyArmLength", ClampMin = "0.0"))
	float ArmLength = 300.f;

	// SpringArm 自身的旋转偏移（Pitch/Yaw/Roll），叠加在臂的最终朝向之上
	// 与相机层级无关：转臂会带着相机一起绕角色公转，而不只是改相机朝向。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bModifyArmRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera", meta = (EditCondition = "bModifyArmRotation"))
	FRotator ArmRotation = FRotator::ZeroRotator;

	// 相机位置偏移（cm）：Y 水平 / Z 高度，相对 SpringArm 末端（+值向右/上，-值反之）
	UPROPERTY(EditAnywhere, Category = "CombatCamera")
	bool bModifyCameraOffsetY = false;

	UPROPERTY(EditAnywhere, Category = "CombatCamera", meta = (EditCondition = "bModifyCameraOffsetY"))
	float CameraOffsetY = 0.f;

	UPROPERTY(EditAnywhere, Category = "CombatCamera")
	bool bModifyCameraOffsetZ = false;

	UPROPERTY(EditAnywhere, Category = "CombatCamera", meta = (EditCondition = "bModifyCameraOffsetZ"))
	float CameraOffsetZ = 0.f;

	// 视场角
	UPROPERTY(EditAnywhere, Category = "CombatCamera")
	bool bModifyFOV = false;

	UPROPERTY(EditAnywhere, Category = "CombatCamera", meta = (EditCondition = "bModifyFOV"))
	float FOV = 90.f;
	
	// 优先级：同时存在多个请求时，取 Priority 最高者；相同则取最近 Push 的。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	int32 Priority = 0;

	// 偏移参考系是否基于角色面朝方向（正后方）进行偏移，而非基于镜头朝向。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bUseCharacterFacingBasis = false;

	// 前置视角：在正后方的基础上把 SpringArm 对齐到「角色朝向 + 180°」，即角色对向
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera", meta = (EditCondition = "bUseCharacterFacingBasis"))
	bool bFrontFacingBasis = false;

	// 对齐到角色正后方（或前置视角）的平滑过渡时间（秒）。仅当 bUseCharacterFacingBasis 时生效。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera", meta=(EditCondition="bUseCharacterFacingBasis", ClampMin="0.0"))
	float CharacterFacingTransitionTime = 0.3f;

	// 勾选后本镜头期间锁定 look 输入，相机不能被LookInput
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bLockLookInput = false;
};

// 相机旋转接管的阶段：None = 未接管；Blending = 过渡中；Holding = 过渡已完成（等待请求结束）
enum class EHijackPhase : uint8
{
	None,
	Blending,
	Holding
};

/**
 * 战斗相机组件：接收 Montage 的相机请求，逐帧解算并混合后写入 SpringArm/Camera。
 * 职责划分：动画（ANS_CombatCamera）只提交「相机意图」，本组件负责「最终解算与写入」，
 */
UCLASS(ClassGroup = (CombatCamera), meta = (BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UCombatCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatCameraComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 推入一个CameraRequest到请求栈，返回一个唯一的请求 ID（供 PopRequest 使用）
	int32 PushRequest(const FCombatCameraRequest& Request);

	// 移除指定Request（Notify End 时调用），该请求的 BlendOutTime 用于淡出
	void PopRequest(int32 RequestId);

	// 清空所有请求（GA 被打断时 End GA 调用兜底)
	void ClearAllRequests();

	// 是否正有激活请求在栈中(当前相机组件是否在工作)
	FORCEINLINE bool HasActiveRequest() const { return ActiveRequests.Num() > 0; }

	// 本组件是否正在接管 SpringArm 的 TargetArmLength。接管期间角色的 Zoom 应让位，否则两边逐帧写同一个值会互相覆盖，臂长抖动
	FORCEINLINE bool IsManagingArmLength() const { return bManagingArmLength; }

	// ── 调试访问器（供 Gameplay Debugger 分类读取当前状态）──
	const FCombatCameraRequest* GetActiveRequest() const { return FindActiveRequest(); }
	FORCEINLINE FVector GetCurrentLocationOffset() const { return CurrentLocationOffset; }
	FORCEINLINE FRotator GetCurrentArmRotationOffset() const { return CurrentArmRotationOffset; }
	FORCEINLINE float GetCurrentArmLength() const { return CurrentArmLength; }
	FORCEINLINE float GetCurrentFOV() const { return CurrentFOV; }
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:
	// 返回当前应生效的请求（Priority 最高；相同则取最新 Push 的）。无请求返回 nullptr
	// OutRequestId 非空时输出该请求的 ID（供接管状态判断Request是否变化），空时直接返回激活的Request
	const FCombatCameraRequest* FindActiveRequest(int32* OutRequestId = nullptr) const;

	// 从 CompOwner 解析 SpringArm/Camera 指针。
	void CacheCameraComponents();

private:
	UPROPERTY()
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY()
	TObjectPtr<UCameraComponent> FollowCamera;

	// 激活请求（requestId -> request），一个模拟栈，ID最高位置即栈位置。
	TMap<int32, FCombatCameraRequest> ActiveRequests;
	int32 NextRequestId = 1;

	// 当前帧混合后的相机参数（逐帧 FInterpTo/RInterpTo 趋近目标）。
	FVector CurrentLocationOffset = FVector::ZeroVector;
	FRotator CurrentArmRotationOffset = FRotator::ZeroRotator;
	float CurrentArmLength = 300.f;
	float CurrentFOV = 90.f;

	// 无战斗相机时的缓存基准值。
	// 所有请求出栈后，各偏移项统一淡出回归到这里的值。
	float BaseArmLength = 300.f;
	float BaseFOV = 90.f;

	// 最近一次 Pop 的请求的 BlendOutTime，用于淡出到基准值。
	float PendingBlendOutTime = 0.35f;

	// 上一帧生效的请求及其 BlendOutTime：用于识别镜头重叠切换，让被顶下去的镜头
	// 按自己的 BlendOutTime 退场（否则它只在栈彻底清空时才被读到，切换时形同虚设）
	int32 PrevActiveRequestId = INDEX_NONE;
	float PrevActiveBlendOutTime = 0.f;

	// ── 臂朝向接管（对齐角色朝向 / 锁 look / 臂旋转偏移）──────────
	// 勾了 bUseCharacterFacingBasis 或 bLockLookInput 的镜头由本组件接管臂朝向；
	// 另外臂旋转偏移未归零时也必须握着写入权，否则偏移会被 bUsePawnControlRotation 顶掉。
	void UpdateBoomRotation(float DeltaTime, const FCombatCameraRequest* ActiveReq, int32 ActiveReqId);

	// 接管开始 / 来源请求变更：记住过渡起点与目标，重启过渡计时。
	void BeginRotationHijack(const FCombatCameraRequest& Request, int32 RequestId);

	// 强制结束接管：清来源标记。写入权的交还由 UpdateBoomRotation 按臂偏移是否归零自然完成。
	void EndRotationHijack();

	// 排查用：把当前接管 / 交还的关键朝向打成一行日志（由 CVar CombatCamera.Debug.Log 控制）。
	// 相机跳变这类问题时打开它，对齐时间线就能看出是「谁在什么时候把朝向拨走的」。
	void LogDebugState(const TCHAR* Tag) const;

	// 角色朝向（+ 180° 前置视角）的水平基准旋转
	FRotator GetCharacterFacingRotation(bool bFrontFacing) const;

	APlayerController* GetOwningPlayerController() const;

	// 当前接管的来源请求 ID（INDEX_NONE = 未接管）。用于识别「换了一个Request接管」。
	int32 HijackRequestId = INDEX_NONE;
	EHijackPhase HijackPhase = EHijackPhase::None;

	// 过渡起点（进入接管瞬间的 Boom 世界旋转）与总时长
	FRotator HijackStartRotation = FRotator::ZeroRotator;
	float HijackBlendTime = 0.3f;
	float HijackElapsed = 0.f;

	// 本次接管的快照（接管期间请求属性可能被优先级更高的请求取代，故记录当时取值）
	bool bHijackUseFacingBasis = false;
	bool bHijackFrontFacing = false;
	bool bHijackLockLook = false;

	// 冻结目标：不基于角色朝向时（仅锁 look）取进入瞬间的视角
	FRotator HijackFrozenRotation = FRotator::ZeroRotator;

	// 进入接管那一帧的角色视角，用于把「接管期间玩家转过的角度」叠回对齐目标上
	FRotator HijackEnterControlRotation = FRotator::ZeroRotator;

	// 臂朝向当前是否由本组件写入（即 bUsePawnControlRotation 被本组件关闭）。
	// 由 true 变 false 的那一帧要把合成朝向折进 ControlRotation，最终视角才不跳。
	bool bBoomOwned = false;

	// 臂旋转偏移是否还没淡出完。没归零前必须一直握着臂的写入权，否则偏移会被 bUsePawnControlRotation 顶掉。
	FORCEINLINE bool IsArmOffsetHeld() const { return !CurrentArmRotationOffset.IsNearlyZero(0.05f); }

	// 把「合成朝向」还原成不含臂旋转偏移的基准朝向。
	// ControlRotation 里存的是基准——持有期间臂上还会再叠一次 CurrentArmRotationOffset，
	// 所以把合成朝向直接写进 ControlRotation 会让偏移叠加两次（镜头会跳一个偏移量）。
	FORCEINLINE FRotator GetLastBoomBaseRotation() const
	{
		return (LastBoomWorldRotation - CurrentArmRotationOffset).GetNormalized();
	}

	// 上一次由本组件写出去的臂世界旋转。交还写入权时必须用它，不能读 SpringArm 的组件旋转：
	// SpringArm 的旋转是相对父组件的（bAbsoluteRotation 默认关），接管期间一旦角色在两次写入
	// 之间转动，组件旋转就会跟着父级漂移。空中居合那种「窗口末尾角色才被 MW 扭 180°」的情况，
	// 交接时读组件旋转会读到漂移后的朝向（正后方），相机就跳回去了。
	FRotator LastBoomWorldRotation = FRotator::ZeroRotator;

	// 本帧是否接管了 SpringArm 的 TargetArmLength
	bool bManagingArmLength = false;
};
