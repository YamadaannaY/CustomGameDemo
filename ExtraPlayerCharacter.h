#pragma once

#include "CoreMinimal.h"
#include "ExtraCharacter.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MotionWarpingComponent.h"
#include "ExtraPlayerCharacter.generated.h"

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
	
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void PawnClientRestart() override;

	virtual void Jump() override;

	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComp; }
	
	bool GetWalkMode() const { return  bWalkMode; }

	// 冲刺模式开关：GA_Evade 截断蒙太奇进入冲刺时置 true；AnimInstance 离开 Sprint 状态经 OnSprintStateLeft 复位
	void SetSprinting(bool bSprinting) { bIsSprinting = bSprinting; }

	FORCEINLINE bool HasForwardInput() const { return bHasMoveInput && ForwardDirectionInput > 0.f; }
	FORCEINLINE bool HasMoveInput() const { return bHasMoveInput; }
	FORCEINLINE float GetRightDirectionInput() const { return RightDirectionInput; }
	FORCEINLINE const FVector& GetInputDirection() const { return InputDirection; }
	FORCEINLINE bool IsMovementInputLocked() const { return bMovementInputLocked; }

	float LastMoveInputDuration = 0.f;
	
	// 锁定/解锁移动输入
	void SetMovementInputLocked(bool bLocked) { bMovementInputLocked = bLocked; }
	
	// 普攻键是否处于按住状态
	FORCEINLINE bool IsHoldingAttack() const { return bHoldingAttack; }

	// 本次按下是否已长按达到重击判定阈值
	FORCEINLINE bool IsLongPressed() const { return bLongPressed; }

	// 重击所需的连段次数（角色BP编辑器可配，默认为3）
	FORCEINLINE float GetHeavyComboCount() const { return HeavyComboCount; }
	
	// 相机组件访问器（供 UCombatCameraComponent 解析写入目标）
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
	FORCEINLINE int32 GetAirEvadeCharges() const { return AirEvadeCharges; }
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
private:
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	USpringArmComponent* CamBoom;

	UPROPERTY(VisibleDefaultsOnly,Category="View")
	UCameraComponent* ViewCam;
	
	UPROPERTY(EditDefaultsOnly,Category="Attack | HeavyAttack")
	float HeavyComboCount = 3.f ; 

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

	// -- 武器输入普攻点按=轻击，长按=重击/连续轻击；两个技能：E=技能(Skill)，Q=大招(Ult)--
	
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

	// -- MotionWarping 组件，用于转身动画的朝向匹配 --
	UPROPERTY(VisibleDefaultsOnly, Category="MotionWarping")
	UMotionWarpingComponent* MotionWarpingComp;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* QuickLeftStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* QuickRightStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnLeft90Montage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnRight90Montage;
	
	//不选择Stop而是Turn的角度阈值
	UPROPERTY(EditDefaultsOnly,Category="Animation | Turn")
	float TurnSharpAngel=110.f;

	// 停步 Montage 被移动输入打断时的 BlendOut 时长（秒）
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	float StopMontageBlendOutTime = 0.15f;

	// 急停时 Capsule 旋转到目标朝向的插值时间（秒）
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	float QuickStopRotationLerpTime = 0.15f;

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

	// 输入方向平滑速度，值越大转向响应越快
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="1.0"))
	float InputDirectionInterpSpeed = 12.f;


	FTimerHandle ArmLengthLerpTimerHandle;

	float TargetArmLength;

	void Move(const FInputActionValue& InputActionValue);
	void StopMoveInput(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);

	friend class UGA_Evade;

	void CalculateTargetDelta(float ForwardInput,float RightInput);
	
	void HandleCameraZoomInput(const FInputActionValue& InputActionValue);
	void ChangeWalkMode(const FInputActionValue& InputActionValue);

	// -- 武器输入处理 --
	
	void OnNormalAttackStarted(const FInputActionValue& InputActionValue);
	void OnNormalAttackCompleted(const FInputActionValue& InputActionValue);
	void OnSkillStarted(const FInputActionValue& InputActionValue);
	void OnUltimateStarted(const FInputActionValue& InputActionValue);
	void OnDodgeStarted(const FInputActionValue& InputActionValue);

	// 按住达到重击阈值时回调：进入长按状态，若段数已满则立即触发重击
	void OnReachHeavyThreshold();

	void LerpArmLength(float Goal);
	void TickArmLengthLerp(float Goal);

	void PlayQuickStopMontage();

	void PlayTurnMontage(bool bTurnLeft);

	// 停步 Montage 正在播放时，收到移动输入即打断
	void CancelStopMontageIfPlaying();

	// 停步/转身 montage 结束（正常播完或被打断）回调
	UFUNCTION()
	void OnStopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 平滑后的输入方向（逐帧 VInterpTo 插值），用于 AddMovementInput
	FVector SmoothedInputDirection = FVector::ZeroVector;

	FVector InputDirection;

	// 由 GA_Evade 截断蒙太奇时设置，下一帧写入 Velocity 以保持冲刺速度（仅当前帧有效）
	FVector SprintTransitionVelocity = FVector::ZeroVector;

	bool bHasMoveInput=false;

	float TargetDelta=0.0f;
	
	float MoveInputStartTime = 0.f;

	float ForwardDirectionInput;

	float RightDirectionInput;

	bool bWalkMode = false ;

	bool bIsSprinting = false;

	// 移动输入锁定标志（空中攻击期间为 true，忽略移动输入）
	bool bMovementInputLocked = false;

	// 攻击键按住状态（按住时连续轻击连段，直到满足重击条件）
	bool bHoldingAttack = false;

	// 本次按下是否已长按达到重击阈值（区分「长按重击」与「高频点按轻击」）
	bool bLongPressed = false;

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
