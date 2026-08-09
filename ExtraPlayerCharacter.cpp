#include "ExtraPlayerCharacter.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "ExtractGameCharacter.h"
#include "ExtraGameAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "WeaponSystem/ExtraGameWeaponTypes.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"


AExtraPlayerCharacter::AExtraPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CamBoom=CreateDefaultSubobject<USpringArmComponent>("Cam Boom");
	CamBoom->SetupAttachment(GetRootComponent());
	CamBoom->bUsePawnControlRotation=true;
	CamBoom->ProbeChannel=ECC_SpringArm;

	ViewCam=CreateDefaultSubobject<UCameraComponent>("View Cam");
	ViewCam->SetupAttachment(CamBoom,USpringArmComponent::SocketName);

	bUseControllerRotationYaw=false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);

	TargetArmLength=CamBoom->TargetArmLength;

	MotionWarpingComp = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));
}

// Called when the game starts or when spawned
void AExtraPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AExtraPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AExtraPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);


	if (UEnhancedInputComponent* InputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		InputComp->BindAction(JumpAction,ETriggerEvent::Started,this,&ThisClass::Jump);
		InputComp->BindAction(JumpAction,ETriggerEvent::Completed,this,&ThisClass::StopJumping);
		InputComp->BindAction(MoveAction,ETriggerEvent::Triggered,this,&ThisClass::Move);
		InputComp->BindAction(MoveAction,ETriggerEvent::Completed,this,&ThisClass::StopMoveInput);
		InputComp->BindAction(LookAction,ETriggerEvent::Triggered,this,&ThisClass::Look);
		InputComp->BindAction(SprintAction,ETriggerEvent::Started,this,&ThisClass::OnSprintActionStarted);
		InputComp->BindAction(SprintAction,ETriggerEvent::Triggered,this,&ThisClass::OnSprintActionTriggered);
		InputComp->BindAction(SprintAction,ETriggerEvent::Completed,this,&ThisClass::OnSprintActionCompleted);
		InputComp->BindAction(CameraZoomInputAction,ETriggerEvent::Triggered,this,&ThisClass::HandleCameraZoomInput);
		InputComp->BindAction(CtrlInputAction,ETriggerEvent::Triggered,this,&ThisClass::ChangeWalkMode);

		// -- 武器输入绑定 --
		// 普攻：点按→轻击 / 长按→重击（通过 Started + Completed 区分时长）
		InputComp->BindAction(NormalAttackAction, ETriggerEvent::Started, this, &ThisClass::OnNormalAttackStarted);
		InputComp->BindAction(NormalAttackAction, ETriggerEvent::Completed, this, &ThisClass::OnNormalAttackCompleted);
		InputComp->BindAction(SkillAction, ETriggerEvent::Started, this, &ThisClass::OnSkillStarted);
		InputComp->BindAction(UltimateAction, ETriggerEvent::Started, this, &ThisClass::OnUltimateStarted);
		InputComp->BindAction(DodgeAction, ETriggerEvent::Started, this, &ThisClass::OnDodgeStarted);
	}

}

void AExtraPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// 在 owning client 上添加 IMC（比 SetupPlayerInputComponent 更可靠）
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (GameplayInputMappingContext)
			{
				InputSystem->AddMappingContext(GameplayInputMappingContext, 0);
			}
		}
	}
}

void AExtraPlayerCharacter::Move(const FInputActionValue& InputActionValue)
{
	FVector2D InputVal=InputActionValue.Get<FVector2d>();

	// 检测 无输入→有输入 的跳变，记录按键开始时间
	if (!bHasMoveInput && !InputVal.IsNearlyZero())
	{
		// 清除延迟停步请求（新输入打断停步等待）
		if (UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			GameAI->ClearStopRequest();
		}

		// 新输入打断急停/转身 montage，短 blend-out 让状态机快速接管
		if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
		{
			AnimInst->Montage_Stop(0.2f, QuickLeftStopMontage);
			AnimInst->Montage_Stop(0.2f, QuickRightStopMontage);
			AnimInst->Montage_Stop(0.2f, TurnLeft90Montage);
			AnimInst->Montage_Stop(0.2f, TurnRight90Montage);
		}

		MoveInputStartTime = GetWorld()->GetTimeSeconds();
	}
	bHasMoveInput = !InputVal.IsNearlyZero();

	if (Controller != nullptr)
	{
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);

		ForwardDirectionInput = InputVal.Y;
		RightDirectionInput = InputVal.X;

		if (!HasCalTargetDelta)
		{
			CalculateTargetDelta(ForwardDirectionInput, RightDirectionInput);
			HasCalTargetDelta = true;
		}

		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

		FVector RawInputWorld = (Forward * ForwardDirectionInput + Right * RightDirectionInput);
		float InputMagnitude = RawInputWorld.Size();

		if (InputMagnitude < KINDA_SMALL_NUMBER)
		{
			SmoothedInputDirection = FVector::ZeroVector;
			InputDirection = FVector::ZeroVector;
			return;
		}

		FVector RawInputDir = RawInputWorld / InputMagnitude;

		if (SmoothedInputDirection.IsNearlyZero())
		{
			SmoothedInputDirection = RawInputDir;
		}
		else
		{
			float DeltaTime = GetWorld()->GetDeltaSeconds();
			SmoothedInputDirection = FMath::VInterpTo(SmoothedInputDirection, RawInputDir, DeltaTime, InputDirectionInterpSpeed);
			SmoothedInputDirection.Normalize();
		}

		InputDirection = SmoothedInputDirection;

		AddMovementInput(SmoothedInputDirection, FMath::Min(InputMagnitude, 1.0f));
	}
}

void AExtraPlayerCharacter::StopMoveInput(const FInputActionValue& InputActionValue)
{
	LastMoveInputDuration = GetWorld()->GetTimeSeconds() - MoveInputStartTime;

	bHasMoveInput = false;
	HasCalTargetDelta = false;
	ForwardDirectionInput = 0.f;
	RightDirectionInput = 0.f;
	InputDirection = FVector::ZeroVector;
	SmoothedInputDirection = FVector::ZeroVector;

	/*// Root Motion montage 控制着角色运动，此时松手不应干预
	if (IsPlayingRootMotion())
	{
		return;
	}*/

	// 先清除旧的停步请求，本次松手会重新决定走哪条路径
	if (UExtraGameAnimInstance* AnimInst = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AnimInst->ClearStopRequest();
	}

	// 轻触判定：输入持续时间 < 0.2s
	if (LastMoveInputDuration > 0.f && LastMoveInputDuration < 0.2f)
	{
		const float AbsTargetDelta = FMath::Abs(TargetDelta);

		if (AbsTargetDelta < 110.f)
		{
			PlayQuickStop();
		}
		else
		{
			const bool bTurnLeft = (TargetDelta < 0.f);
			PlayTurnMontage(bTurnLeft);
		}
	}
	else
	{
		if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			AI->RequestStop();
		}
	}
}

void AExtraPlayerCharacter::Jump()
{
	Super::Jump();

	// 跳跃时清除停步请求，防止落地后 FootPlant Notify 误触发 Stop
	if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AI->ClearStopRequest();
	}
}

void AExtraPlayerCharacter::Look(const FInputActionValue& InputActionValue)
{
	if (Controller != nullptr)
	{
		FVector2D LookAxisVector = InputActionValue.Get<FVector2D>();

		FRotator CurrentRotation = Controller->GetControlRotation();

		const float MaxDegreesPerSecond = 360.f;

		const float DelatTime = GetWorld()->GetDeltaSeconds();
		const float MaxDegreesPerFrame = MaxDegreesPerSecond*DelatTime;

		float DesiredYawInput = LookAxisVector.X * 0.4f;
		float DesiredPitchInput = LookAxisVector.Y * 0.4f;

		DesiredYawInput = FMath::Clamp(DesiredYawInput,-MaxDegreesPerFrame,MaxDegreesPerFrame);
		DesiredPitchInput = FMath::Clamp(DesiredPitchInput,-MaxDegreesPerFrame,MaxDegreesPerFrame);

		AddControllerYawInput(DesiredYawInput);
		AddControllerPitchInput(-DesiredPitchInput);
	}
}

void AExtraPlayerCharacter::OnSprintActionStarted(const FInputActionValue& InputActionValue)
{
}

void AExtraPlayerCharacter::OnSprintActionTriggered(const FInputActionValue& InputActionValue)
{
}

void AExtraPlayerCharacter::OnSprintActionCompleted(const FInputActionValue& InputActionValue)
{

}

void AExtraPlayerCharacter::HandleCameraZoomInput(const FInputActionValue& InputActionValue)
{
	const float ZoomValue=InputActionValue.Get<float>();
	TargetArmLength=FMath::Clamp(TargetArmLength + ZoomValue * ZoomStepSize, MinArmLength, MaxArmLength);

	LerpArmLength(TargetArmLength);
}

void AExtraPlayerCharacter::ChangeWalkMode(const FInputActionValue& InputActionValue)
{
	bWalkMode = !bWalkMode ;
}

void AExtraPlayerCharacter::CalculateTargetDelta(float ForwardInput,float RightInput)
{
	FRotator ControlRot = GetControlRotation();
	ControlRot.Roll = 0.0f;
	ControlRot.Pitch = 0.0f;

	FVector ForwardVector = ControlRot.Vector();
	FVector RightVector = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Y);

	FVector DesiredDirection = (ForwardVector * ForwardInput) + (RightVector * RightInput);
	if (!DesiredDirection.IsNearlyZero())
	{
		DesiredDirection.Normalize();
	}

	FRotator ActorRot = GetActorRotation();
	FVector CurrentForward = ActorRot.Vector();

	float DesiredYaw = DesiredDirection.Rotation().Yaw;
	float CurrentYaw = CurrentForward.Rotation().Yaw;

	TargetDelta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
}

void AExtraPlayerCharacter::LerpArmLength(float Goal)
{
	GetWorldTimerManager().ClearTimer(ArmLengthLerpTimerHandle);
	ArmLengthLerpTimerHandle=GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,&ThisClass::TickArmLengthLerp,Goal));
}

void AExtraPlayerCharacter::TickArmLengthLerp(float Goal)
{
	const float CurrentArmLength=CamBoom->TargetArmLength;

	if (FMath::Abs(CurrentArmLength - Goal) < 1.f)
	{
		CamBoom->TargetArmLength=Goal;
		return;
	}

	const float LerpAlpha=FMath::Clamp(GetWorld()->GetDeltaSeconds() * ZoomLerpSpeed, 0.f, 1.f);
	const float NewArmLength=FMath::Lerp(CurrentArmLength, Goal, LerpAlpha);

	CamBoom->TargetArmLength=NewArmLength;

	ArmLengthLerpTimerHandle=GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,&ThisClass::TickArmLengthLerp,Goal));
}


void AExtraPlayerCharacter::PlayQuickStop()
{
	if (TargetDelta < 0.f && QuickLeftStopMontage)
	{
		PlayAnimMontage(QuickLeftStopMontage);
	}
	else if (TargetDelta > 0.f && QuickRightStopMontage)
	{
		PlayAnimMontage(QuickRightStopMontage);
	}
}

void AExtraPlayerCharacter::PlayTurnMontage(bool bTurnLeft)
{
	UAnimMontage* MontageToPlay = bTurnLeft ? TurnLeft90Montage : TurnRight90Montage;

	if (!MontageToPlay)
	{
		return;
	}

	const float TurnYawOffset = bTurnLeft ? -TargetDelta : TargetDelta;
	const FRotator CurrentRot = GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, CurrentRot.Yaw + TurnYawOffset, CurrentRot.Roll);

	if (MotionWarpingComp)
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("TurnTarget");
		WarpTarget.Location = GetActorLocation();
		WarpTarget.Rotation = TargetRot;
		MotionWarpingComp->AddOrUpdateWarpTarget(WarpTarget);
	}

	PlayAnimMontage(MontageToPlay, TurnMontagePlayRate);
}

// ──────────────────────────────────────────────────────────────
// 武器输入处理（鸣潮风格）
// ──────────────────────────────────────────────────────────────

void AExtraPlayerCharacter::OnNormalAttackStarted(const FInputActionValue& InputActionValue)
{
	NormalAttackPressTime = GetWorld()->GetTimeSeconds();
}

void AExtraPlayerCharacter::OnNormalAttackCompleted(const FInputActionValue& InputActionValue)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	const float Elapsed = GetWorld()->GetTimeSeconds() - NormalAttackPressTime;
	const EWeaponAbilityInputID InputID = (Elapsed >= HeavyAttackHoldTime)
		? EWeaponAbilityInputID::HeavyAttack
		: EWeaponAbilityInputID::LightAttack;

	AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(InputID));
}

void AExtraPlayerCharacter::OnSkillStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityLocalInputPressed(
			static_cast<int32>(EWeaponAbilityInputID::Skill));
	}
}

void AExtraPlayerCharacter::OnUltimateStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityLocalInputPressed(
			static_cast<int32>(EWeaponAbilityInputID::Ultimate));
	}
}

void AExtraPlayerCharacter::OnDodgeStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityLocalInputPressed(
			static_cast<int32>(EWeaponAbilityInputID::Dodge));
	}
}
