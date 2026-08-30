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
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"
#include "AbilitySystemBlueprintLibrary.h"


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

	CombatCameraComp = CreateDefaultSubobject<UCombatCameraComponent>(TEXT("CombatCamera"));
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

	if (!SprintTransitionVelocity.IsNearlyZero())
	{
		GetCharacterMovement()->Velocity = SprintTransitionVelocity;
		SprintTransitionVelocity = FVector::ZeroVector;
	}

	// Sprint 时调高 MaxWalkSpeed，退出时恢复默认
	if (bIsSprinting)
	{
		float SprintMax = GetCharacterMovement()->MaxWalkSpeed * SprintSpeedMultiplier;
		GetCharacterMovement()->MaxWalkSpeed = FMath::FInterpTo(
			GetCharacterMovement()->MaxWalkSpeed, SprintMax, DeltaTime, 5.f);
	}
	else
	{
		float DefaultMax = 600.f;
		GetCharacterMovement()->MaxWalkSpeed = FMath::FInterpTo(
			GetCharacterMovement()->MaxWalkSpeed, DefaultMax, DeltaTime, 5.f);
	}

	// 维护空中的 GameplayTag（用于 GA activation 过滤）
	if (AbilitySystemComponent)
	{
		const bool bAirborne = GetCharacterMovement()->IsFalling();
		const FGameplayTag AirborneTag = UUExtraAbilitySystemStatic::GetAirborneTag();
		if (bAirborne && !AbilitySystemComponent->HasMatchingGameplayTag(AirborneTag))
		{
			AbilitySystemComponent->AddLooseGameplayTag(AirborneTag);
		}
		else if (!bAirborne && AbilitySystemComponent->HasMatchingGameplayTag(AirborneTag))
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(AirborneTag);
		}
	}
}

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
		if (UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			GameAI->ClearStopRequest();
		}

		CancelStopMontageIfPlaying();

		MoveInputStartTime = GetWorld()->GetTimeSeconds();
	}
	bHasMoveInput = !InputVal.IsNearlyZero();

	// 锁定移动输入：仍更新Input标志（供 Evade 打断时判断前/后空翻），但不产生移动
	if (bMovementInputLocked)
	{
		return;
	}

	if (Controller != nullptr)
	{
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);

		ForwardDirectionInput = InputVal.Y;
		RightDirectionInput = InputVal.X;

		// 每帧用「当前朝向 vs 当前输入方向」重算 TargetDelta，而非只在首帧算一次：
		// 用 bOrientRotationToMovement 逐帧转向，松手瞬间的 TargetDelta 应是
		// 最新的剩余角度差（转向到位→小角度→急停；快速反向还没转到位→大角度→转身 montage）。
		// 旧的 HasCalTargetDelta 只在首帧计算，转身后松手仍用初始值，导致左右判断错乱。
		CalculateTargetDelta(ForwardDirectionInput, RightDirectionInput);
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
			// 用角度插值而非向量插值：向量插值在 180° 反向时会经过零点，
			// Normalize 后方向不变，导致直接反向前进卡死。角度插值能正确处理反向转身。
			const float DeltaTime = GetWorld()->GetDeltaSeconds();
			const float CurrentYaw = SmoothedInputDirection.Rotation().Yaw;
			const float TargetYaw = RawInputDir.Rotation().Yaw;
			const float DeltaYaw = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
			const float StepYaw = DeltaYaw * FMath::Clamp(DeltaTime * InputDirectionInterpSpeed, 0.f, 1.f);
			SmoothedInputDirection = FRotationMatrix(FRotator(0.f, CurrentYaw + StepYaw, 0.f)).GetUnitAxis(EAxis::X);
		}

		InputDirection = SmoothedInputDirection;

		AddMovementInput(SmoothedInputDirection, FMath::Min(InputMagnitude, 1.0f));
	}
}

void AExtraPlayerCharacter::StopMoveInput(const FInputActionValue& InputActionValue)
{
	LastMoveInputDuration = GetWorld()->GetTimeSeconds() - MoveInputStartTime;

	bHasMoveInput = false;
	ForwardDirectionInput = 0.f;
	RightDirectionInput = 0.f;
	InputDirection = FVector::ZeroVector;
	SmoothedInputDirection = FVector::ZeroVector;

	
	// 只在普通跑步时才响应松手停步，避免干扰 Evade / QuickStop / Turn 等 Montage
	if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
	{
		if (AnimInst->IsAnyMontagePlaying())
		{
			return;
		}
	}

	// 轻触判定：输入持续时间 < 0.2s
	if (LastMoveInputDuration > 0.f && LastMoveInputDuration < 0.2f)
	{
		const float AbsTargetDelta = FMath::Abs(TargetDelta);

		if (AbsTargetDelta < TurnSharpAngel)
		{
			// 急停：轻触 + 小角度差，由 rootmotion 自行减速到停，不锁速 
			PlayQuickStop();
		}
		else
		{
			// 转身：轻触 + 大角度差（快速反向还没转到位），由 MotionWarping 转向，不锁速
			const bool bTurnLeft = (TargetDelta < 0.f);
			PlayTurnMontage(bTurnLeft);
		}
	}
	else
	{
		// 普通停步：锁速等待 FootPlant 进入停步状态机。
		// 锁速只用于非 rootmotion 的普通停步；QuickStop / Turn 是 rootmotion，
		// 若也锁速会每帧覆盖 rootmotion 的速度并把 GroundSpeed 冻结，导致「原地跑步」。
		if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			AI->ClearStopRequest();
			AI->RequestStop();
		}
	}
}

void AExtraPlayerCharacter::Jump()
{
	Super::Jump();

	// 跳跃打断所有正在激活的GA（走各GA的EndAbility收尾：停Montage、恢复RootMotion、清状态tag等）
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities(nullptr);
	}

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
	if (DesiredDirection.IsNearlyZero())
	{
		// 无明确输入方向（摇杆回中边缘），保留上一次的 TargetDelta：
		// 零向量的 Rotation().Yaw 恒为 0，直接计算会把 TargetDelta 污染成「朝向 vs 世界 0°」。
		return;
	}
	DesiredDirection.Normalize();

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
	// TargetDelta == 0 时角色朝向已与输入方向对齐（急停后静止、再朝原方向短输入最典型），
	// 左右急停无差别，用 <= 兜底到左停，避免两个分支都不进导致静默失败、无法再次触发急停。
	if (TargetDelta <= 0.f && QuickLeftStopMontage)
	{
		PlayAnimMontage(QuickLeftStopMontage);
	}
	else if (TargetDelta > 0.f && QuickRightStopMontage)
	{
		PlayAnimMontage(QuickRightStopMontage);
	}

	if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
	{
		AnimInst->OnMontageEnded.RemoveAll(this);
		AnimInst->OnMontageEnded.AddDynamic(this, &AExtraPlayerCharacter::OnStopMontageEnded);
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

	PlayAnimMontage(MontageToPlay);

	// 转身 montage 结束（播完/被打断）时清零停步请求与残留速度，
	// 否则 StopMoveInput 里先 RequestStop() 锁的速度会在 montage 播完后残留，导致 idle 滑行。
	if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
	{
		AnimInst->OnMontageEnded.RemoveAll(this);
		AnimInst->OnMontageEnded.AddDynamic(this, &AExtraPlayerCharacter::OnStopMontageEnded);
	}
}

void AExtraPlayerCharacter::OnStopMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != QuickLeftStopMontage &&
		Montage != QuickRightStopMontage &&
		Montage != TurnLeft90Montage &&
		Montage != TurnRight90Montage)
	{
		return;
	}

	if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AI->ClearStopRequest();

		// 清零残留速度（原 ClearStopAndVelocity 职责）：防止停步/转身 montage 播完后角色仍滑行
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->Velocity = FVector::ZeroVector;
		}
	}
}

void AExtraPlayerCharacter::CancelStopMontageIfPlaying()
{
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// 停步 Montage（急停/转身）都带 rootmotion，输入恢复时直接打断进入跑步
	UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
	if (ActiveMontage == QuickLeftStopMontage ||
		ActiveMontage == QuickRightStopMontage ||
		ActiveMontage == TurnLeft90Montage ||
		ActiveMontage == TurnRight90Montage)
	{
		AnimInst->StopAllMontages(StopMontageBlendOutTime);
	}
}

// ──────────────────────────────────────────────────────────────
// 武器输入处理
// ──────────────────────────────────────────────────────────────

void AExtraPlayerCharacter::OnNormalAttackStarted(const FInputActionValue& InputActionValue)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	bHoldingAttack = true;
	bLongPressed = false;

	// 启动重击长按阈值定时器：按住达到 HeavyAttackHoldTime 才进入「长按重击」判定
	GetWorldTimerManager().SetTimer(
		HeavyAttackHoldTimerHandle,
		this,
		&ThisClass::OnReachHeavyThreshold,
		HeavyAttackHoldTime,
		false);

	// 按下即发送轻击输入，触发/继续连段（连段是否自动衔接由 GA 内的按住判定决定）
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		this, UUExtraAbilitySystemStatic::GetLightAttackInputTag(), FGameplayEventData());
}

void AExtraPlayerCharacter::OnNormalAttackCompleted(const FInputActionValue& InputActionValue)
{
	bHoldingAttack = false;
	bLongPressed = false;

	// 若未达阈值即松开（点按），取消定时器：本次判定为轻击，不触发重击
	if (GetWorldTimerManager().IsTimerActive(HeavyAttackHoldTimerHandle))
	{
		GetWorldTimerManager().ClearTimer(HeavyAttackHoldTimerHandle);
	}
}

void AExtraPlayerCharacter::OnReachHeavyThreshold()
{
	bLongPressed = true;

	if (!AbilitySystemComponent)
	{
		return;
	}

	// 长按达到阈值时段数已满 3，立即重击；未满则由 GA 内切入帧在连段满 3 后触发
	const float ComboCount = AbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetComboCountAttribute());
	if (ComboCount >= HeavyComboCount)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, UUExtraAbilitySystemStatic::GetHeavyAttackInputTag(), FGameplayEventData());
	}
}

void AExtraPlayerCharacter::OnSkillStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, UUExtraAbilitySystemStatic::GetSkillInputTag(), FGameplayEventData());
	}
}

void AExtraPlayerCharacter::OnUltimateStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, UUExtraAbilitySystemStatic::GetUltimateInputTag(), FGameplayEventData());
	}
}

void AExtraPlayerCharacter::OnDodgeStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, UUExtraAbilitySystemStatic::GetDodgeInputTag(), FGameplayEventData());
	}
}