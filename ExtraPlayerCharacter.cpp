#include "ExtraPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "ExtractGameCharacter.h"
#include "ExtraGameAnimInstance.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"
#include "ExtractGameCharacter/LockOn/ULockOnComponent.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"


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

	LockOnComponent = CreateDefaultSubobject<ULockOnComponent>(TEXT("LockOn"));
}

AActor* AExtraPlayerCharacter::GetLockTarget() const
{
	return LockOnComponent ? LockOnComponent->GetLockTarget() : nullptr;
}

void AExtraPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void AExtraPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SprintTransitionVelocity.IsNearlyZero())
	{
		GetCharacterMovement()->Velocity = SprintTransitionVelocity;
		SprintTransitionVelocity = FVector::ZeroVector;
	}
	
	const float TargetMaxSpeed = bIsSprinting
		? SprintSpeed
		: (bWalkMode ? WalkSpeed : RunSpeed);
	GetCharacterMovement()->MaxWalkSpeed = FMath::FInterpTo(GetCharacterMovement()->MaxWalkSpeed, TargetMaxSpeed, DeltaTime, 5.f);

	// 维护空中状态的 GameplayTag
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
			// 落地瞬间重置本次浮空的空中闪避预算（下次浮空重新从初始值开始）
			ResetAirEvadeCharges();
		}
	}
}

void AExtraPlayerCharacter::ConsumeAirEvade()
{
	if (AirEvadeCharges > 0)
	{
		--AirEvadeCharges;
	}
}

void AExtraPlayerCharacter::GrantAirEvadeCharge()
{
	// 每浮空仅首次恢复有效：保证总空中闪避不超过 MaxAirEvadeCharges 次
	if (bAirEvadeBonusGranted)
	{
		return;
	}
	bAirEvadeBonusGranted = true;
	AirEvadeCharges = FMath::Min(AirEvadeCharges + 1, MaxAirEvadeCharges);
}

void AExtraPlayerCharacter::ResetAirEvadeCharges()
{
	AirEvadeCharges = FMath::Min(InitialAirEvadeCharges, MaxAirEvadeCharges);
	bAirEvadeBonusGranted = false;
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
		InputComp->BindAction(WalkRunSwitchInputAction, ETriggerEvent::Started, this, &ThisClass::ChangeWalkMode);

		// -- 武器输入绑定 --
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

	// 锁定移动输入判断
	if (bMovementInputLocked)
	{
		return;
	}

	if (Controller != nullptr)
	{
		//以摄像机旋转为前方向
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);

		//Vector2D中X为前后，Y为右左，是平面坐标系
		ForwardDirectionInput = InputVal.Y;
		RightDirectionInput = InputVal.X;

		// 每帧用「当前朝向 vs 当前输入方向」重算 TargetDelta，供松手瞬间判断急停(QuickStop)还是转身(Turn) montage。
		CalculateTargetDelta(ForwardDirectionInput, RightDirectionInput);

		//EAxis中X为前Y为右Z为上，是世界坐标轴
		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

		//获得输入向量及其数值
		const FVector RawInputWorld = (Forward * ForwardDirectionInput + Right * RightDirectionInput);
		const float InputMagnitude = RawInputWorld.Size();

		if (InputMagnitude < KINDA_SMALL_NUMBER)
		{
			InputDirection = FVector::ZeroVector;
			return;
		}

		//获取输入方向
		const FVector RawInputDir = RawInputWorld / InputMagnitude;

		// 转向已收归 MovementComp（角度差自适应速率）负责：输入层直接提交真实输入方向，
		InputDirection = RawInputDir;

		AddMovementInput(RawInputDir, FMath::Min(InputMagnitude, 1.0f));
	}
}

void AExtraPlayerCharacter::StopMoveInput(const FInputActionValue& InputActionValue)
{
	LastMoveInputDuration = GetWorld()->GetTimeSeconds() - MoveInputStartTime;

	bHasMoveInput = false;

	// 松开移动输入即退出冲刺模式（速度由 Tick 插值回落，停步走既有流程）；
	// 同时撤销 Sprint 进入闸门，防止「截断瞬间松手 → 之后普通跑动误进 Sprint」
	SetSprinting(false);
	if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AI->bEvadeToSprint = false;
	}

	ForwardDirectionInput = 0.f;
	RightDirectionInput = 0.f;
	InputDirection = FVector::ZeroVector;
	
	// 仅在仍有非停步类 Montage（Evade / 攻击等）播放时才跳过松手停步，避免松手去打断战斗动作。
	// 不能用 IsAnyMontagePlaying() 判断：它等价于 MontageInstances.Num()>0，
	// 被本次按下打断、正处 BlendOut 的停步 Montage 仍留在实例数组中会使其返回 true，
	// 导致快速连点变向时松手若落在此BlendOut窗口内会被直接吞掉，无法重新触发快速停步。
	if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
	{
		if (UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage())
		{
			if (ActiveMontage != QuickLeftStopMontage &&
				ActiveMontage != QuickRightStopMontage &&
				ActiveMontage != TurnLeft90Montage &&
				ActiveMontage != TurnRight90Montage)
			{
				return;
			}
		}
	}

	// 轻触判定：输入持续时间 < 0.2s
	if (LastMoveInputDuration > 0.f && LastMoveInputDuration < 0.2f)
	{
		const float AbsTargetDelta = FMath::Abs(TargetDelta);

		if (AbsTargetDelta < TurnSharpAngel)
		{
			// 急停
			PlayQuickStopMontage();
		}
		else
		{
			// 转身
			const bool bTurnLeft = (TargetDelta < 0.f);
			PlayTurnMontage(bTurnLeft);
		}
	}
	//移动超过0.2s
	else
	{
		// 停步：锁速等待 FootPlant 进入停步状态机。
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

	// 跳跃时清除停步请求，防止落地后误触发 Stop
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

		float DesiredYawInput = LookAxisVector.X * 0.8f;
		float DesiredPitchInput = LookAxisVector.Y * 0.5f;

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
		// 无明确输入方向则保留上一次的 TargetDelta
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


void AExtraPlayerCharacter::PlayQuickStopMontage()
{
	UAnimMontage* MontageToPlay = (TargetDelta <= 0.f) ? QuickLeftStopMontage : QuickRightStopMontage;
	if (!MontageToPlay)
	{
		return;
	}

	// 松手瞬间 TargetDelta 仍是「当前朝向 → 触发方向」的剩余转角。用MW旋转让急停动画精确落在触发朝向，避免角色停在半转的中间朝向。
	const FRotator CurrentRot = GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, CurrentRot.Yaw + TargetDelta, CurrentRot.Roll);

	if (MotionWarpingComp)
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("QuickStopTarget");
		WarpTarget.Location = GetActorLocation();
		WarpTarget.Rotation = TargetRot;
		MotionWarpingComp->AddOrUpdateWarpTarget(WarpTarget);
	}

	PlayAnimMontage(MontageToPlay);

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

		/*
		// 清零残留速度防止停步/转身 montage 播完后角色仍滑行
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->Velocity = FVector::ZeroVector;
		}*/
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
		AnimInst->Montage_StopWithBlendOut(ActiveMontage->BlendOut);
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

	// 按下即发送轻击输入。
	// 空中按形态分派：一阶段直接打空中攻击，二阶段交给二阶段空中连打 GA，
	FGameplayTag AttackTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling()
		&& !AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase2StateTag()))
	{
		AttackTag = UUExtraAbilitySystemStatic::GetAirDiveInputTag();
	}

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, AttackTag, FGameplayEventData());
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

	// 松手广播：二阶段蓄力重击 GA 监听此事件，收到即停当前段播结束段打出攻击
	// （无订阅者时无副作用；一阶段重击 GA 不监听）
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		this, UUExtraAbilitySystemStatic::GetHeavyAttackReleaseInputTag(), FGameplayEventData());
}

void AExtraPlayerCharacter::OnReachHeavyThreshold()
{
	bLongPressed = true;

	if (!AbilitySystemComponent)
	{
		return;
	}

	// 二阶段：纯长按达阈值即触发重击（无需连段打满），是否真正激活由 GA 的 State.Phase2 门控裁决
	if (AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase2StateTag()))
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, UUExtraAbilitySystemStatic::GetHeavyAttackInputTag(), FGameplayEventData());
		return;
	}

	// 一阶段：需打满能量（EnergyValue 达 EnergyMaxValue）才触发重击
	if (AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase1StateTag()))
	{
		const float EnergyValue = AbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
		if (EnergyValue >= GetHeavyComboEnergyNeed())
		{
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				this, UUExtraAbilitySystemStatic::GetHeavyAttackInputTag(), FGameplayEventData());
		}
	}
}

float AExtraPlayerCharacter::GetHeavyComboEnergyNeed() const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyMaxValueAttribute());
	}
	return 0.f;
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
			this, UUExtraAbilitySystemStatic::
			GetUltimateInputTag(), FGameplayEventData());
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