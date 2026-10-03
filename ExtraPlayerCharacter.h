#pragma once

#include "CoreMinimal.h"
#include "ExtraCharacter.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MotionWarpingComponent.h"
#include "ExtraPlayerCharacter.generated.h"

// 松手时客户端判定出的动作意图。判定依赖客户端的输入时长与 TargetYaw，服务器算不出来，
// 因此随 Server_SetMoveInputState 一并上传，由服务器执行与客户端相同的分支，各模拟端也使用服务端的权威值播对应动画。
UENUM()
enum class EMoveReleaseAction : uint8
{
	None,           // 不触发停步（本次松手被战斗 Montage 接管）
	RequestStop,    // 长按：等客户端落脚点起播后通知服务器（服务器不再自己等落脚点）
	QuickStopLeft,  // 轻触 + 小角差：左急停 Montage
	QuickStopRight, // 轻触 + 小角差：右急停 Montage
	TurnLeft,       // 轻触 + 大角差：左转身 Montage
	TurnRight,      // 轻触 + 大角差：右转身 Montage
	StopLeft,       // 长按停步（左脚）：由服务器接受一下两个值，不要服务器自己算了（导致抖动，直接客户端一个RPC传过来）
	StopRight,      // 长按停步（右脚）
};

UCLASS()
class EXTRACTGAMECHARACTER_API AExtraPlayerCharacter : public AExtraCharacter
{
	GENERATED_BODY()

public:
	AExtraPlayerCharacter(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void PawnClientRestart() override;

	virtual void Jump() override;

	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComp; }
	
	bool GetWalkMode() const { return  bWalkMode; }

	// 冲刺模式开关：GA_Evade 截断蒙太奇进入冲刺时置 true；AnimInstance 离开 Sprint 状态经 OnSprintStateLeft 复位
	void SetSprinting(bool bSprinting) { bIsSprinting = bSprinting; }

	FORCEINLINE bool HasMoveInput() const { return bHasMoveInput; }

	//获取当前输入相对于摄像机视角的方向
	FORCEINLINE const FVector& GetInputDirection() const { return InputDirection; }

	float LastMoveInputDuration = 0.f;

	// 锁定/解锁移动输入
	void SetMovementInputLocked(bool bLocked) { bMovementInputLocked = bLocked; }
	
	// 普攻键是否处于按住状态
	FORCEINLINE bool IsHoldingAttack() const { return bHoldingAttack; }

	// 本次按下是否已长按达到重击判定阈值
	FORCEINLINE bool IsLongPressed() const { return bLongPressed; }

	// 技能键（E）是否仍被按住：长按到居合检测帧才允许交接进居合
	FORCEINLINE bool IsHoldingSkill() const { return bHoldingSkill; }

	// Skill01 释放后置位：下一次普攻连段直接从最后一段起（该段额外充能）
	void MarkSkill01ComboBoost() { bSkill01ComboBoost = true; }

	// 读取并清位（由 UGA_Combo 激活时调用，保证只有「下一次」普攻受用）
	bool ConsumeSkill01ComboBoost();

	// 从DT中获取重击所需的能量值
	float GetHeavyComboEnergyNeed() const;
	
	// 相机组件访问器（供 UCombatCameraComponent 解析）
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CamBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return ViewCam; }

	// 锁定组件访问器（攻击 GA 经此读取锁定目标）
	FORCEINLINE class ULockOnComponent* GetLockOnComponent() const { return LockOnComponent; }

	// 当前锁定目标（转发到锁定组件，无组件/无目标时为空）
	AActor* GetLockTarget() const;

	// ── 空中闪避次数（本次浮空的空中 Evade 预算）─────────────────────
	// 空中 Evade 激活消耗 1 次；空中攻击恢复 1 次（每浮空仅首次生效）；
	// 预算耗尽后空中 Evade 无法再激活，落地时重置。保证每次浮空最多 MaxAirEvadeCharges 次空中闪避。
	FORCEINLINE bool CanUseAirEvade() const { return AirEvadeCharges > 0; }
	void ConsumeAirEvade();
	// 空中攻击恢复一次空中闪避（每浮空仅首次生效，最高恢复到上限）
	void GrantAirEvadeCharge();
	// 落地时重置本次浮空的空中闪避预算
	void ResetAirEvadeCharges();

	
	//Walk模式最大速度
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0.0"))
	float WalkSpeed = 250.f;

	//Run模式最大速度
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0.0"))
	float RunSpeed = 600.f;
	
	//Sprint（冲刺）最高速度，物理 MaxWalkSpeed 与动画 PlayRate 归一统一引用此值
	UPROPERTY(EditDefaultsOnly, Category="Movement|Sprint", meta=(ClampMin="0.0"))
	float SprintSpeed = 800.f;
	
	// 长按停步 Montage
	void PlayStopMontage(bool bLeft);

	//RPC发给服务端让它也播，只慢一个RPC发送时间
	UFUNCTION(Server, Reliable)
	void Server_NotifyStopMontagePlayed(bool bLeft);
private:
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	USpringArmComponent* CamBoom;

	UPROPERTY(VisibleDefaultsOnly,Category="View")
	UCameraComponent* ViewCam;

	// 战斗相机组件：接收 Montage 相机请求，逐帧解算写入 SpringArm/Camera 进行摄像机更新
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	class UCombatCameraComponent* CombatCameraComp;

	// 自动锁定最近敌方单位组件（本地检测）
	UPROPERTY(VisibleDefaultsOnly, Category = "LockOn")
	class ULockOnComponent* LockOnComponent;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputMappingContext* GameplayInputMappingContext;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputAction* JumpAction ;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputAction* MoveAction ;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputAction* LookAction ;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputAction* SprintAction ;

	UPROPERTY(EditDefaultsOnly,Category="Input")
	UInputAction* CameraZoomInputAction;

	UPROPERTY(EditDefaultsOnly, Category="Input")
	UInputAction* WalkRunSwitchInputAction;

	// -- 武器输入普攻点按=轻击，长按=重击/连续轻击；两个技能：E=技能(Skill)，R=大招(Ult)--
	
	UPROPERTY(EditDefaultsOnly, Category="Input|Weapon")
	UInputAction* NormalAttackAction;

	UPROPERTY(EditDefaultsOnly, Category="Input|Weapon")
	UInputAction* SkillAction;

	UPROPERTY(EditDefaultsOnly, Category="Input|Weapon")
	UInputAction* UltimateAction;

	UPROPERTY(EditDefaultsOnly, Category="Input|Weapon")
	UInputAction* DodgeAction;

	// 长按判定阈值（秒），超过此时间为重击，低于为轻击
	UPROPERTY(EditDefaultsOnly, Category="Input|Weapon", meta=(ClampMin="0.1"))
	float HeavyAttackHoldTime = 0.25f;
	
	UPROPERTY(VisibleDefaultsOnly, Category="MotionWarping")
	UMotionWarpingComponent* MotionWarpingComp;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* LeftStopRunMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* RightStopRunMontage;
	
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* LeftStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* RightStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnLeft90Montage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnRight90Montage;
	
	//不选择Stop而是原地Turn的角度阈值
	UPROPERTY(EditDefaultsOnly,Category="Animation | Turn")
	float TurnSharpAngel=110.f;
	
	//弹簧臂最小长度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float MinArmLength=20.f;

	//弹簧臂最大长度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float MaxArmLength=400.f;

	//鼠标滚轮每格的缩放步长
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float ZoomStepSize=50.f;

	//缩放的Lerp速度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float ZoomLerpSpeed=10.f;
	
	FTimerHandle ArmLengthLerpTimerHandle;

	float TargetArmLength = 0.f;

	// 客户端 → 服务器：上传移动输入状态与松手动作意图，用Reliable保证输入状态准确
	UFUNCTION(Server, Reliable)
	void Server_SetMoveInputState(bool bNewHasMoveInput, EMoveReleaseAction ReleaseAction, float InTargetYaw);

	// 客户端 → 服务器：上报客户端算好的角色朝向。
	// 服务器缺少即时的输入与相机数据，无法复现客户端的双层插值转向逻辑，因此使用客户端的结果
	// Unreliable：高频且可丢，丢一帧只是服务器朝向停一帧，下一帧即补上
	UFUNCTION(Server, Unreliable)
	void Server_SyncClientYaw(float Yaw);
	
	//客户端输入层触发Ctrl，切换WalkMode，服务端抄写这个值
	UFUNCTION(Server, Reliable)
	void Server_ChangeWalkMode(bool WalkMode);

	void Move(const FInputActionValue& InputActionValue);
	void StopMoveInput(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);

	friend class UGA_Evade;

	void CalculateTargetDelta(float ForwardInput,float RightInput);

	// 当前朝向 → 目标朝向（TargetYaw）的最短角差（度），左负右正。
	float GetTargetDelta() const;
	
	void HandleCameraZoomInput(const FInputActionValue& InputActionValue);
	void ChangeWalkMode(const FInputActionValue& InputActionValue);

	// -- 武器输入处理 --
	
	void OnNormalAttackStarted(const FInputActionValue& InputActionValue);
	void OnNormalAttackCompleted(const FInputActionValue& InputActionValue);
	void OnSkillStarted(const FInputActionValue& InputActionValue);

	// 技能键松手：清按住标志（长按到检测帧才进居合，中途松开即放弃）
	void OnSkillCompleted(const FInputActionValue& InputActionValue);
	void OnUltimateStarted(const FInputActionValue& InputActionValue);
	void OnDodgeStarted(const FInputActionValue& InputActionValue);

	// 按住达到重击阈值时回调：进入长按状态，若段数已满则立即触发重击
	void OnReachHeavyThreshold();

	void LerpArmLength(float Goal);
	void TickArmLengthLerp(float Goal);

	// bLeft 由客户端按 TargetDelta 判定后传入，服务端复用这个结果
	void PlayQuickStopMontage(bool bLeft);

	void PlayTurnMontage(bool bTurnLeft);
	
	// 停步 Montage 正在播放时，收到移动输入即打断
	void CancelStopMontageIfPlaying();

	// 统一的动作 Montage 播放入口：模拟代理会从服务器当前进度起播（见实现里的注释）
	void PlayActionMontage(UAnimMontage* MontageToPlay);

	// 停步/转身 montage 结束（正常播完或被打断）回调
	UFUNCTION()
	void OnStopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	FVector InputDirection;

	// 由 GA_Evade 截断蒙太奇时设置，下一帧写入 Velocity 以保持冲刺速度（仅当前帧有效）
	FVector SprintTransitionVelocity = FVector::ZeroVector;

	// 上一帧 RepRootMotion 是否处于播放中：模拟代理用它检测「服务器停播」的下降沿
	bool bWasRepRootMotionActive = false;

	// 是否持有移动输入,用来做服务端的加速度判断（客户端CMC的Acc是不会复制的）
	UPROPERTY(Replicated)
	bool bHasMoveInput=false;

	// 服务器权威的「本轮松手要播的急停/转身动作」，复制给模拟代理
	UPROPERTY(Replicated)
	EMoveReleaseAction RepPendingAction = EMoveReleaseAction::None;

	// 服务器权威的目标朝向：急停 / 转身 / 停步 Montage 上的 MotionWarping 都拿它当目标。
	// 模拟代理本地没有输入，TargetYaw 会一直是默认值，必须靠这份复制值覆盖，
	// 否则 MW 会把远端角色转到错误方向。
	UPROPERTY(Replicated)
	float RepTargetYaw = 0.f;

	// 模拟代理去重：本轮 RepPendingAction 是否已消费（它要到下次移动才复位）
	bool bRepActionConsumed = false;

	// 目标朝向的世界 Yaw：由最后一次有效移动输入方向算出，与角色当前朝向无关。
	// 存绝对方向而不是相对角差，是为了让转身/急停的目标不受按下到松手期间CMC转过的角度的影响
	float TargetYaw = 0.f;
	
	float MoveInputStartTime = 0.f;

	float ForwardDirectionInput;

	float RightDirectionInput;

	UPROPERTY(Replicated)
	bool bWalkMode = false;

	bool bIsSprinting = false;

	// 移动输入锁定标志（空中攻击期间为 true，忽略移动输入）
	bool bMovementInputLocked = false;

	// 攻击键按住状态（按住时连续轻击连段，直到满足重击条件）
	bool bHoldingAttack = false;

	// 本次按下是否已长按达到重击阈值（区分「长按重击」与「高频点按轻击」）
	bool bLongPressed = false;

	// Skill键按住状态（长按到居合检测帧才进居合）
	bool bHoldingSkill = false;

	// Skill01 释放后的下一次普攻强化待消费标记（跳第三段 + 额外充能）
	bool bSkill01ComboBoost = false;

	// 重击长按阈值定时器
	FTimerHandle HeavyAttackHoldTimerHandle;

	// ── 空中闪避次数 ──
	// 每次浮空初始可用的空中闪避次数（落地重置为初始值）
	UPROPERTY(EditDefaultsOnly, Category = "Evade|Air", meta = (ClampMin = "1"))
	int32 InitialAirEvadeCharges = 1;

	// 每浮空允许的空中闪避次数上限（空中攻击的恢复不会超过此值）
	UPROPERTY(EditDefaultsOnly, Category = "Evade|Air", meta = (ClampMin = "1"))
	int32 MaxAirEvadeCharges = 2;

	// 本次浮空剩余的空中闪避次数
	int32 AirEvadeCharges = 1;

	// 本次浮空是否已用过空中攻击的恢复机会（限制总空中闪避不超过 MaxAirEvadeCharges）
	bool bAirEvadeBonusGranted = false;
};
