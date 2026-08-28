#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UCombatCameraComponent.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * 战斗相机Request：动画 Notify State（ANS_CombatCamera）在 Montage 期间提交的相机意图。
 *
 * 约定：LocationOffset / RotationOffset 是相对 SpringArm 末端（USpringArmComponent::SocketName）
 * 的偏移，叠加在原先的ViewCam 上。ArmLength 为绝对目标值，激活期间接管 SpringArm 的 TargetArmLength，
 * 如果需要基于默认（即摄像机朝向角色正前方，游戏开始时的镜头）进行偏移，则打开bUseCharacterFacingBasis
 * 结束时统一重置
 * 
 * 这是一个栈结构，具有Push和Pop功能，激活离当前时间最近且优先级最高的那一个Request
 */
USTRUCT(BlueprintType)
struct FCombatCameraRequest
{
	GENERATED_BODY()

	// 相机相对 SpringArm 末端的位置偏移（cm）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	FVector LocationOffset = FVector::ZeroVector;

	// 相机相对 SpringArm 末端的旋转偏移（Pitch/Yaw/Roll）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	FRotator RotationOffset = FRotator::ZeroRotator;

	// 目标相机臂长度（cm）。激活期间覆盖 Zoom 的 ArmLength。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float ArmLength = 300.f;

	// 目标 FOV。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float FOV = 90.f;

	// 淡入时间（秒）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float BlendInTime = 0.15f;

	// 淡出时间（秒）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	float BlendOutTime = 0.2f;

	// 优先级：同时存在多个请求时，取 Priority 最高者；相同则取最近 Push 的。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	int32 Priority = 0;

	// 偏移参考系是否基于角色面朝方向（正后方），而非跟随镜头朝向。
	// 开启后相机忽略鼠标旋转，固定在「角色正后方 + 偏移」。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bUseCharacterFacingBasis = false;

	// 切换到角色正后方视角的平滑过渡时间（秒）。仅当 bUseCharacterFacingBasis 时生效。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera", meta=(EditCondition="bUseCharacterFacingBasis", ClampMin="0.0"))
	float CharacterFacingTransitionTime = 0.3f;
	
	//使用Basis会锁视角，此时look输入无效，如果要打开锁定，则开启此选项
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatCamera")
	bool bUnlockCharacterFacing = false;
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

	// 推入一个相机请求，返回请求 ID（供 PopRequest 使用）。
	int32 PushRequest(const FCombatCameraRequest& Request);

	// 移除指定请求（Notify End 时调用），携带该请求的 BlendOutTime 用于淡出。
	void PopRequest(int32 RequestId);

	// 清空所有请求（GA 被打断时兜底，防止请求泄漏导致相机卡在战斗态）。
	void ClearAllRequests();

	// 是否正有激活请求（供外部判断当前是否处于战斗相机态）。
	FORCEINLINE bool HasActiveRequest() const { return ActiveRequests.Num() > 0; }

	// ── 调试访问器（供 Gameplay Debugger 分类读取当前状态）──
	const FCombatCameraRequest* GetActiveRequest() const { return FindActiveRequest(); }
	FORCEINLINE FVector GetCurrentLocationOffset() const { return CurrentLocationOffset; }
	FORCEINLINE FRotator GetCurrentRotationOffset() const { return CurrentRotationOffset; }
	FORCEINLINE float GetCurrentArmLength() const { return CurrentArmLength; }
	FORCEINLINE float GetCurrentFOV() const { return CurrentFOV; }
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

protected:
	// 返回当前应生效的请求（Priority 最高；相同则取最新 Push 的）。无请求返回 nullptr。
	const FCombatCameraRequest* FindActiveRequest() const;

	// 首次接管前缓存 SpringArm/Camera 的当前值作为「无战斗相机」的基准。
	void CacheBaseIfNeeded();

	// 从 Owner 解析 SpringArm/Camera 指针。
	void CacheCameraComponents();

private:
	UPROPERTY()
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY()
	TObjectPtr<UCameraComponent> FollowCamera;

	// 激活请求（requestId -> request）。
	TMap<int32, FCombatCameraRequest> ActiveRequests;
	int32 NextRequestId = 1;

	// 当前混合后的状态（逐帧 FInterpTo/RInterpTo 趋近目标）。
	FVector CurrentLocationOffset = FVector::ZeroVector;
	FRotator CurrentRotationOffset = FRotator::ZeroRotator;
	float CurrentArmLength = 300.f;
	float CurrentFOV = 90.f;

	// 无战斗相机时的基准值（首次 PushRequest 时从 SpringArm/Camera 缓存）。
	float BaseArmLength = 300.f;
	float BaseFOV = 90.f;
	
	//是否已经缓存初始值
	bool bBaseCached = false;

	// 最近一次 Pop 的请求的 BlendOutTime，用于淡出到基准值。
	float PendingBlendOutTime = 0.2f;

	// 退出「固定正后方」模式：把 control rotation 同步到角色正后方后再交还鼠标控制，
	// 使摄像机归位到正后方（而非弹回玩家重置之前的位置）。
	void ExitCharacterFacingMode();

	// 是否处于「固定正后方」模式（由某镜头开启，全局持久到所有请求清空后退出）。
	bool bCharacterFacingMode = false;

	// 进入正后方模式时的平滑过渡速度（= 1/过渡时间）。
	float CharacterFacingTransitionSpeed = 3.33f;
};
