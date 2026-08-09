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
private:
	UPROPERTY(VisibleDefaultsOnly,Category="View")
	class USpringArmComponent* CamBoom;

	UPROPERTY(VisibleDefaultsOnly,Category="View")
	class UCameraComponent* ViewCam;

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

	// 转身 Montage 播放速率（>1 更快）
	UPROPERTY(EditDefaultsOnly, Category="Animation|Turn")
	float TurnMontagePlayRate = 1.2f;

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

	FTimerHandle ArmLengthLerpTimerHandle;

	float TargetArmLength;

	void Move(const FInputActionValue& InputActionValue);
	void StopMoveInput(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);
	void OnSprintActionStarted(const FInputActionValue& InputActionValue);
	void OnSprintActionTriggered(const FInputActionValue& InputActionValue);
	void OnSprintActionCompleted(const FInputActionValue& InputActionValue);
	void HandleCameraZoomInput(const FInputActionValue& InputActionValue);
	void ChangeWalkMode(const FInputActionValue& InputActionValue);
	void CalculateTargetDelta(float ForwardInput,float RightInput);

	// -- 武器输入处理 --
	void OnNormalAttackStarted(const FInputActionValue& InputActionValue);
	void OnNormalAttackCompleted(const FInputActionValue& InputActionValue);
	void OnSkillStarted(const FInputActionValue& InputActionValue);
	void OnUltimateStarted(const FInputActionValue& InputActionValue);
	void OnDodgeStarted(const FInputActionValue& InputActionValue);
	void LerpArmLength(float Goal);
	void TickArmLengthLerp(float Goal);

	// 急停动画（abs(TargetDelta) < 110，朝向由 bOrientRotationToMovement 处理）
	void PlayQuickStop();

	// 90转身 montage + MotionWarping（abs(TargetDelta) > 110）
	void PlayTurnMontage(bool bTurnLeft);

	// 平滑后的输入方向（逐帧 VInterpTo 插值），用于 AddMovementInput
	FVector SmoothedInputDirection = FVector::ZeroVector;

	FVector InputDirection;

	bool HasCalTargetDelta = false ;

	bool bHasMoveInput=false;

	float TargetDelta=0.0f;

	float LastMoveInputDuration = 0.f;

	float MoveInputStartTime = 0.f;

	float ForwardDirectionInput;

	float RightDirectionInput;

	bool bWalkMode = false ;

	// 记录普攻按下时间，用于点按/长按判定
	float NormalAttackPressTime = 0.f;
};
