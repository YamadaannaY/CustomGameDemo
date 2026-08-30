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
	// Sets default values for this character's properties
	AExtraPlayerCharacter(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void PawnClientRestart() override;

	virtual void Jump() override;

	bool GetWalkMode() { return  bWalkMode;}
	FORCEINLINE bool HasForwardInput() const { return bHasMoveInput && ForwardDirectionInput > 0.f; }
	FORCEINLINE bool HasMoveInput() const { return bHasMoveInput; }
	FORCEINLINE float GetRightDirectionInput() const { return RightDirectionInput; }
	FORCEINLINE const FVector& GetInputDirection() const { return InputDirection; }
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComp; }

	// 锁定/解锁移动输入（空中攻击期间由 GA 调用，禁止角色水平移动）
	void SetMovementInputLocked(bool bLocked) { bMovementInputLocked = bLocked; }
	FORCEINLINE bool IsMovementInputLocked() const { return bMovementInputLocked; }

	// 攻击键是否处于按住状态（供 GA 判断「按住 = 连续轻击」自动连段）
	FORCEINLINE bool IsHoldingAttack() const { return bHoldingAttack; }

	// 本次按下是否已长按达到重击阈值（区分「长按重击」与「高频点按轻击」）
	FORCEINLINE bool IsLongPressed() const { return bLongPressed; }

	// 重击所需的连段次数（编辑器可配，默认 3）
	FORCEINLINE float GetHeavyComboCount() const { return HeavyComboCount; }
	
	float LastMoveInputDuration = 0.f;

	// 相机组件访问器（供 UCombatCameraComponent 解析写入目标）
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CamBoom; }
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return ViewCam; }
private:
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	USpringArmComponent* CamBoom;

	UPROPERTY(VisibleDefaultsOnly,Category="View")
	UCameraComponent* ViewCam;
	
	UPROPERTY(EditDefaultsOnly,Category="Attack | HeavyAttack")
	float HeavyComboCount = 3.f ; 

	// 战斗相机组件：接收 Montage 相机请求，逐帧解算写入 SpringArm/Camera
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	class UCombatCameraComponent* CombatCameraComp;

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
	UInputAction* CtrlInputAction;

	// -- 武器输入（鸣潮风格：普攻点按=轻击，长按=重击；两个技能：E=技能，Q=大招）--
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
	float HeavyAttackHoldTime = 0.35f;

	// -- MotionWarping 组件，用于转身动画的朝向匹配 --
	UPROPERTY(VisibleDefaultsOnly, Category="MotionWarping")
	UMotionWarpingComponent* MotionWarpingComp;

	// -- 急停 / 转身动画 Montage --
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* QuickLeftStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	UAnimMontage* QuickRightStopMontage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnLeft90Montage;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	UAnimMontage* TurnRight90Montage;
	
	UPROPERTY(EditDefaultsOnly,Category="Animation | Turn")
	float TurnSharpAngel=110.f;

	// 停步 Montage 被移动输入打断时的 BlendOut 时长（秒）
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	float StopMontageBlendOutTime = 0.15f;

	// 急停时 Capsule 旋转到目标朝向的插值时间（秒）
	UPROPERTY(EditDefaultsOnly, Category="Animation|Stop")
	float QuickStopRotationTime = 0.15f;

	//弹簧臂最小长度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float MinArmLength=200.f;

	//弹簧臂最大长度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float MaxArmLength=800.f;

	//鼠标滚轮每格的缩放步长
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float ZoomStepSize=50.f;

	//缩放的Lerp速度
	UPROPERTY(EditDefaultsOnly,Category="View|Zoom")
	float ZoomLerpSpeed=10.f;

	// 输入方向平滑速度，值越大转向响应越快
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="1.0"))
	float InputDirectionInterpSpeed = 12.f;

	// Sprint 时的额外速度乘数（1.x = 微快，2.0 = 双倍）
	UPROPERTY(EditDefaultsOnly, Category="Movement|Sprint", meta=(ClampMin="1.0"))
	float SprintSpeedMultiplier = 1.8f;

	FTimerHandle ArmLengthLerpTimerHandle;

	float TargetArmLength;

	void Move(const FInputActionValue& InputActionValue);
	void StopMoveInput(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);

	friend class UGA_Evade;

	void HandleCameraZoomInput(const FInputActionValue& InputActionValue);
	void ChangeWalkMode(const FInputActionValue& InputActionValue);
	void CalculateTargetDelta(float ForwardInput,float RightInput);

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

	// 急停动画（abs(TargetDelta) < 110，朝向由 bOrientRotationToMovement 处理）
	void PlayQuickStop();

	// 90转身 montage + MotionWarping（abs(TargetDelta) > 110）
	void PlayTurnMontage(bool bTurnLeft);

	// 停步 Montage 正在播放时，收到移动输入即打断（停掉 rootmotion Montage 回到跑步）
	void CancelStopMontageIfPlaying();

	// 停步/转身 montage 结束（正常播完或被打断）回调：清停步请求 + 清零残留速度
	UFUNCTION()
	void OnStopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 平滑后的输入方向（逐帧 VInterpTo 插值），用于 AddMovementInput
	FVector SmoothedInputDirection = FVector::ZeroVector;

	FVector InputDirection;

	// 由 GA_Evade::OnEvadeToSprint 设置，触发 Sprint 过渡（仅当前帧有效）
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
};
