#include "ExtraPlayerCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "ExtractGameCharacter.h"
#include "ExtraGameAnimInstance.h"
#include "ExtraGameMovementComponent.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayAbility.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"
#include "ExtractGameCharacter/LockOn/ULockOnComponent.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* GetMoveActionName(EMoveReleaseAction Action)
	{
		switch (Action)
		{
		case EMoveReleaseAction::None:           return TEXT("None");
		case EMoveReleaseAction::QuickStopLeft:  return TEXT("QuickStopLeft");
		case EMoveReleaseAction::QuickStopRight: return TEXT("QuickStopRight");
		case EMoveReleaseAction::TurnLeft:       return TEXT("TurnLeft");
		case EMoveReleaseAction::TurnRight:      return TEXT("TurnRight");
		case EMoveReleaseAction::RequestStop:    return TEXT("RequestStop");
		case EMoveReleaseAction::StopLeft:       return TEXT("StopLeft");
		case EMoveReleaseAction::StopRight:      return TEXT("StopRight");
		case EMoveReleaseAction::StopWalkLeft:   return TEXT("StopWalkLeft");
		case EMoveReleaseAction::StopWalkRight:  return TEXT("StopWalkRight");
		default:                                 return TEXT("Unknown");
		}
	}

	const TCHAR* GetLogRole(const AActor* Actor)
	{
		return (Actor && Actor->GetLocalRole() == ROLE_Authority) ? TEXT("S") : TEXT("C");
	}
}


AExtraPlayerCharacter::AExtraPlayerCharacter(const FObjectInitializer& ObjectInitializer): Super(ObjectInitializer)
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

void AExtraPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 只发给其他客户端（跳过 owner）：服务端依赖的就是本地客户端算出来的值，无需下发。
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, bHasMoveInput, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, bWalkMode, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, RepPendingAction, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, RepActionSerial, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, RepTargetYaw, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter, bIsJumping, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtraPlayerCharacter,InputDirection,COND_SkipOwner);
}

void AExtraPlayerCharacter::Server_SetMoveInputState_Implementation(bool bNewHasMoveInput, EMoveReleaseAction ReleaseAction, float InTargetYaw)
{
	/**	本地输入算出来的值，抄写到服务端	**/
	bHasMoveInput = bNewHasMoveInput;
	TargetYaw = InTargetYaw;
	RepTargetYaw = InTargetYaw;
	
	UExtraGameAnimInstance* AI = GetMesh() ? Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;

	if (bNewHasMoveInput)
	{
		// 移动：与客户端 Move 的跳变处理对应，撤销残留的停步请求
		if (AI)
		{
			AI->ClearStopRequest();
		}

		// 同样打断停步 Montage
		CancelStopMontageIfPlaying();

		// 重新移动：本轮松手动作全部作废，模拟代理据此复位它的"已消费"标记
		RepPendingAction = EMoveReleaseAction::None;
		return;
	}

	//松开移动进入的逻辑判断
	UExtraGameMovementComponent* MoveComp = Cast<UExtraGameMovementComponent>(GetCharacterMovement());
	switch (ReleaseAction)
	{
	case EMoveReleaseAction::RequestStop:
		if (AI)
		{
			//松手那一刻重置一次
			AI->ClearStopRequest();
		}
		//切换 CMC 的停步减速度
		if (MoveComp)
		{
			MoveComp->SetStopRequested(true);
		}

		RepPendingAction = EMoveReleaseAction::None;
		break;

	// 轻触的急停/转身不需要等落脚点：立即播放
	case EMoveReleaseAction::QuickStopLeft:
		// 与客户端 CancelStopMontageIfPlaying 对齐：不要在急停期间继续用停步刹车
		if (MoveComp)
		{
			MoveComp->SetStopRequested(false);
		}
		PlayQuickStopMontage(true);
		RepPendingAction = ReleaseAction;
		++RepActionSerial;
		break;

	case EMoveReleaseAction::QuickStopRight:
		if (MoveComp)
		{
			MoveComp->SetStopRequested(false);
		}
		PlayQuickStopMontage(false);
		RepPendingAction = ReleaseAction;
		++RepActionSerial;
		break;

	case EMoveReleaseAction::TurnLeft:
		if (MoveComp)
		{
			MoveComp->SetStopRequested(false);
		}
		PlayTurnMontage(true);
		RepPendingAction = ReleaseAction;
		++RepActionSerial;
		break;

	case EMoveReleaseAction::TurnRight:
		if (MoveComp)
		{
			MoveComp->SetStopRequested(false);
		}
		PlayTurnMontage(false);
		RepPendingAction = ReleaseAction;
		++RepActionSerial;
		break;

	default:
		if (AI)
		{
			AI->ClearStopRequest();
		}
		RepPendingAction = EMoveReleaseAction::None;
		break;
	}
}

void AExtraPlayerCharacter::Server_NotifyStopMontagePlayed_Implementation(bool bLeft, bool bInWalkMode)
{
	PlayStopMontageForMode(bLeft, 0.f, bInWalkMode);

	// 模拟代理据此立即播放，Tick中每帧等待这个Rep值。
	// 走/跑停步用不同的 Action，保证代理和服务器选同一份 Montage
	RepPendingAction = bInWalkMode
		? (bLeft ? EMoveReleaseAction::StopWalkLeft : EMoveReleaseAction::StopWalkRight)
		: (bLeft ? EMoveReleaseAction::StopLeft : EMoveReleaseAction::StopRight);
	++RepActionSerial;

	// 停步 Montage 接管位移，CMC 不必再走停步减速度
	if (UExtraGameMovementComponent* MoveComp = Cast<UExtraGameMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->SetStopRequested(false);
	}
}

void AExtraPlayerCharacter::Server_SetHoldingAttack_Implementation(bool bHolding)
{
	// 服务端据此判定「仍按住」，在自己的段变更帧续段（判定点见 UGA_ComboHeavy::OnComboSectionChanged）
	bHoldingAttack = bHolding;
}

void AExtraPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 三档速度是移动曲线横轴（0停/1走/2跑/3冲刺）的锚点，统一从这里同步给移动组件
	if (UExtraGameMovementComponent* MoveComp = Cast<UExtraGameMovementComponent>(GetCharacterMovement()))
	{
		MoveComp->SetGaitSpeeds(WalkSpeed, RunSpeed, SprintSpeed);
	}
}

void AExtraPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (HasAuthority() || IsLocallyControlled())
	{
		bIsJumping = JumpCurrentCount > 0;
	}

	// 模拟代理：跟随服务器播急停/转身 Montage，并在服务器停播 RM Montage 时收掉本地那份。
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		// 目标朝向以服务器为准：本地没有输入，TargetYaw 一直是默认值，
		// 直接用它会让这些 Montage 上的 MotionWarping 把角色转到错误方向
		TargetYaw = RepTargetYaw;

		// 先处理"服务器停播根运动"这个下降沿，再播新动作，
		// 否则新动作可能在同一帧被这次收尾一起取消掉
		const bool bRepRootMotionActive = GetRepRootMotion().bIsActive;
		if (bWasRepRootMotionActive && !bRepRootMotionActive)
		{
			CancelStopMontageIfPlaying();
		}
		bWasRepRootMotionActive = bRepRootMotionActive;

		// 急停/转身/停步：服务器每播一次动作序号就自增，代理按序号变化播一次。
		// 不依赖观察到 None，None 与动作值可能落在同一次网络更新里
		if (RepActionSerial != LastSeenActionSerial)
		{
			LastSeenActionSerial = RepActionSerial;

			// 收到就播：和服务器只差一个 RTT 的进度，引擎的位置修正量很小。
			// 不要试图等 RepRootMotion 对齐再按服务器进度起播——那样反而会把
			// 起播位置和代理当前的根运动错开，触发大步长修正（跳变 + GetSimulationTimeStep 警告）
			// 清掉上一轮播放留下的服务器根运动采样。
			// RepRootMotion 只要还是 active 就不会清这个数组，同一段蒙太奇重播时
			// SimulatedRootMotionPositionFixup 会匹配到旧记录，差值可能超过 1 秒
			RootMotionRepMoves.Empty();

			PlayProxyAction(RepPendingAction, 0.f);
			
		}

		// 本地播放的停步/转身蒙太奇和服务器根运动进度做一次对齐。
		// 偏差过大时，引擎的 SimulatedRootMotionPositionFixup 会把 DeltaPosition/PlayRate
		// 当作模拟步长去追位置，表现为跳变 + GetSimulationTimeStep 连续警告。
		// 这里把偏差压到 0.15 秒以内，既避免大步长，也不会打断正常播放
		const FRepRootMotionMontage& ServerRootMotion = GetRepRootMotion();
		if (ServerRootMotion.bIsActive && ServerRootMotion.Animation)
		{
			if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				if (FAnimMontageInstance* MontageInst = AnimInst->GetActiveMontageInstance())
				{
					const UAnimSequenceBase* MontageAsset = MontageInst->Montage.Get();
					if (ServerRootMotion.Animation.Get() == MontageAsset)
					{
						const float Drift = MontageInst->GetPosition() - ServerRootMotion.Position;
						if (FMath::Abs(Drift) > 0.15f)
						{
							MontageInst->SetPosition(ServerRootMotion.Position);
						}
					}
				}
			}
		}
	}

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

			// 同时清空空中普攻段数：段数只在同一次腾空内记，落地后重新从段1 打起
			AbilitySystemComponent->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetAirAttackStageTag(), 0);
		}

		// 离开二阶段：清空ProTag,回一阶段后再进二阶段，要从头重新打满三次居合）。
		if (!AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase2StateTag()))
		{
			const FGameplayTag JuheCountTag = UUExtraAbilitySystemStatic::GetProJuheCountTag();
			if (AbilitySystemComponent->GetTagCount(JuheCountTag) > 0)
			{
				AbilitySystemComponent->SetLooseGameplayTagCount(JuheCountTag, 0);
			}

			const FGameplayTag ProReadyTag = UUExtraAbilitySystemStatic::GetProReadyTag();
			if (AbilitySystemComponent->HasMatchingGameplayTag(ProReadyTag))
			{
				AbilitySystemComponent->SetLooseGameplayTagCount(ProReadyTag, 0);
			}
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
		InputComp->BindAction(SkillAction, ETriggerEvent::Completed, this, &ThisClass::OnSkillCompleted);
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

void AExtraPlayerCharacter::Server_ChangeWalkMode_Implementation(bool WalkMode)
{
	bWalkMode = WalkMode ;  
}

void AExtraPlayerCharacter::Move(const FInputActionValue& InputActionValue)
{
	FVector2D InputVal=InputActionValue.Get<FVector2d>();

	// 检测 无输入→有输入 的跳变，记录按键开始时间
	if (!bHasMoveInput && !InputVal.IsNearlyZero())
	{
		MoveInputStartTime = GetWorld()->GetTimeSeconds();

		const bool bInStopPipeline = IsStopPipelineActive();
		bResumedFromStop = bInStopPipeline;
		
		// 按下即结束停步：清停步请求、停掉停步/转身 Montage，并通知服务端恢复移动。
		// 后续 AddMovementInput 当帧生效，由 CMC 接管移动与旋转（RootMotion Montage 不再挡路）。
		if (UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			GameAI->ClearStopRequest();
		}

		CancelStopMontageIfPlaying();

		Server_SetMoveInputState(true, EMoveReleaseAction::None, 0.f);
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

		//EAxis中X为前Y为右Z为上，是世界坐标轴
		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

		//获得输入向量及其数值
		const FVector RawInputWorld = (Forward * InputVal.Y + Right * InputVal.X);
		const float InputMagnitude = RawInputWorld.Size();

		if (InputMagnitude < KINDA_SMALL_NUMBER)
		{
			InputDirection = FVector::ZeroVector;
			return;
		}

		//获取输入方向单位向量
		const FVector RawInputDir = RawInputWorld / InputMagnitude;

		//Vector2D中Y为前后，X为右左，是平面坐标系
		ForwardDirectionInput = InputVal.Y;
		RightDirectionInput = InputVal.X;

		// 每帧用「当前输入方向」更新目标朝向，供松手瞬间判断急停(QuickStop)还是转身(Turn) montage。
		CalculateTargetDelta(ForwardDirectionInput, RightDirectionInput);

		InputDirection = RawInputDir;
		
		Server_SendInputDirection(InputDirection);
		
		AddMovementInput(RawInputDir, FMath::Min(InputMagnitude, 1.0f));
	}
}

void AExtraPlayerCharacter::StopMoveInput(const FInputActionValue& InputActionValue)
{
	LastMoveInputDuration = GetWorld()->GetTimeSeconds() - MoveInputStartTime;

	const bool bResumed = bResumedFromStop;
	bResumedFromStop = false;
	

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
			if (ActiveMontage != LeftRunStopMontage &&
				ActiveMontage != RightRunStopMontage &&
				ActiveMontage != LeftWalkStopMontage &&
				ActiveMontage != RightWalkStopMontage &&
				ActiveMontage != LeftQuickStopRunMontage &&
				ActiveMontage != RightQuickStopRunMontage &&
				ActiveMontage != TurnLeft90Montage &&
				ActiveMontage != TurnRight90Montage)
			{
				// 输入状态仍要同步给服务器，但不触发停步（本次松手被战斗动作接管）
				Server_SetMoveInputState(false, EMoveReleaseAction::None, TargetYaw);
				return;
			}
		}
	}

	// 本次按下是从停步管线里“抢回来”的：按下已取消停步并恢复输入。
	// 短按松手回到“对应方向的急停”；按住松手走正常停步。
	if (bResumed)
	{
		if (LastMoveInputDuration < MoveHoldResumeTime)
		{
			// 短按：直接播两个配置急停 Montage 中的对应侧，不等落脚点。
			// 左右选择沿用急停逻辑：TargetDelta <= 0 取左。
			const float TargetDelta = GetTargetDelta();
			const bool bLeft = (TargetDelta <= 0.f);
			PlayQuickStopMontage(bLeft);

			const EMoveReleaseAction Action = bLeft
				? EMoveReleaseAction::QuickStopLeft
				: EMoveReleaseAction::QuickStopRight;
			
			Server_SetMoveInputState(false, Action, TargetYaw);
			return;
		}

		// 按住：正常停步
		if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			AI->ClearStopRequest();
			AI->RequestStop();
		}
		
		Server_SetMoveInputState(false, EMoveReleaseAction::RequestStop, TargetYaw);
		return;
	}

	// 正常松手：直接结算，由对应分支自己发 RPC。
	// 不要在这之前先发一次 (false, None)：它会把 RepPendingAction 立刻压成 None，
	// 紧接着的动作值会被代理漏掉（重构前就是这个行为，代理能正常播）
	ResolveStopDecision(LastMoveInputDuration);
}

bool AExtraPlayerCharacter::IsStopPipelineActive() const
{
	const UExtraGameAnimInstance* AI = GetMesh() ? Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (AI && AI->bRequestStop)
	{
		return true;
	}

	return IsStopMontagePlaying();
}

bool AExtraPlayerCharacter::IsStopMontagePlaying() const
{
	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		return false;
	}

	// 停步/急停/转身都要认，缺一组就会把正在播的停步当成"没有停步"，后续判定全错
	const UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage();
	return ActiveMontage == LeftRunStopMontage
		|| ActiveMontage == RightRunStopMontage
		|| ActiveMontage == LeftWalkStopMontage
		|| ActiveMontage == RightWalkStopMontage
		|| ActiveMontage == LeftQuickStopRunMontage
		|| ActiveMontage == RightQuickStopRunMontage
		|| ActiveMontage == TurnLeft90Montage
		|| ActiveMontage == TurnRight90Montage;
}

void AExtraPlayerCharacter::ResolveStopDecision(float HoldDuration)
{
	const float TargetDelta = GetTargetDelta();
	
	// 挂起期间可能已经进了战斗动作：这时放弃本次停步，别把停步/急停压在攻击 Montage 上
	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (UAnimMontage* ActiveMontage = AnimInst->GetCurrentActiveMontage())
		{
			if (ActiveMontage != LeftRunStopMontage &&
				ActiveMontage != RightRunStopMontage &&
				ActiveMontage != LeftWalkStopMontage &&
				ActiveMontage != RightWalkStopMontage &&
				ActiveMontage != LeftQuickStopRunMontage &&
				ActiveMontage != RightQuickStopRunMontage &&
				ActiveMontage != TurnLeft90Montage &&
				ActiveMontage != TurnRight90Montage)
			{
				return;
			}
		}
	}

	// 松手只按时长分流，不看速度：
	// - 短于 MoveHoldResumeTime 是点按：按角差选急停/转身
	// - 其余是停步，锁速等落脚点，由 AnimInstance 在 FootPlant 起播
	if (HoldDuration > 0.f && HoldDuration < MoveHoldResumeTime)
	{
		const float AbsTargetDelta = FMath::Abs(TargetDelta);

		// 左右方向由本端判定后一并上传：服务器朝向虽已同步但仍有微差，重算可能选反
		EMoveReleaseAction Action ;
		if (AbsTargetDelta < TurnSharpAngel)
		{
			// 急停：左右选择沿用急停逻辑
			const bool bLeft = (TargetDelta <= 0.f);
			PlayQuickStopMontage(bLeft);
			Action = bLeft ? EMoveReleaseAction::QuickStopLeft : EMoveReleaseAction::QuickStopRight;
		}
		else
		{
			// 转身
			const bool bTurnLeft = (TargetDelta < 0.f);
			PlayTurnMontage(bTurnLeft);
			Action = bTurnLeft ? EMoveReleaseAction::TurnLeft : EMoveReleaseAction::TurnRight;
		}
		
		Server_SetMoveInputState(false, Action, TargetYaw);
	}
	// 按住达到/超过 MoveHoldResumeTime：正常停步
	else
	{
		// 停步：锁速等待落脚点，由 AnimInstance 在落脚点起播，并通知服务器同一瞬间起播
		if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			AI->ClearStopRequest();
			AI->RequestStop();
		}
		
		Server_SetMoveInputState(false, EMoveReleaseAction::RequestStop, TargetYaw);
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

	CancelStopMontageIfPlaying();
	bResumedFromStop = false;	// 起跳打断本次按下/松手判定，落地后重新按过才算
	// RootMotion 驱动的前冲（Evade 等）会在 Velocity 上算出一个非常快的速度：
	// Jump 只设置 Z 分量，空中无水平阻尼，残速在整段滞空生效导致跳得极远。此处收拢到地面速度上限。
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const FVector HorizontalVelocity(Movement->Velocity.X, Movement->Velocity.Y, 0.f);
		const float MaxHorizontalSpeed = Movement->MaxWalkSpeed;
		if (HorizontalVelocity.SizeSquared() > FMath::Square(MaxHorizontalSpeed))
		{
			const FVector ClampedVelocity = HorizontalVelocity.GetSafeNormal() * MaxHorizontalSpeed;
			Movement->Velocity.X = ClampedVelocity.X;
			Movement->Velocity.Y = ClampedVelocity.Y;
		}
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

		float DesiredYawInput = LookAxisVector.X;
		float DesiredPitchInput = LookAxisVector.Y;

		DesiredYawInput = FMath::Clamp(DesiredYawInput,-MaxDegreesPerFrame,MaxDegreesPerFrame);
		DesiredPitchInput = FMath::Clamp(DesiredPitchInput,-MaxDegreesPerFrame,MaxDegreesPerFrame);

		AddControllerYawInput(DesiredYawInput);
		AddControllerPitchInput(-DesiredPitchInput);
	}
}

void AExtraPlayerCharacter::HandleCameraZoomInput(const FInputActionValue& InputActionValue)
{
	// 战斗镜头正在接管臂长时整体忽略输入：两边逐帧写同一个 TargetArmLength 会互相覆盖，臂长抖。
	// 这里不动 TargetArmLength，镜头组件在不接管时会把自己的基准同步到 SpringArm 实际长度，
	// 所以退出战斗后滚轮仍然从可见的长度继续。
	if (CombatCameraComp && CombatCameraComp->IsManagingArmLength())
	{
		return;
	}

	const float ZoomValue=InputActionValue.Get<float>();
	TargetArmLength=FMath::Clamp(TargetArmLength + ZoomValue * ZoomStepSize, MinArmLength, MaxArmLength);

	LerpArmLength(TargetArmLength);
}

void AExtraPlayerCharacter::ChangeWalkMode(const FInputActionValue& InputActionValue)
{
	bWalkMode = !bWalkMode ;
	Server_ChangeWalkMode(bWalkMode);
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
		// 无明确输入方向则保留上一次的目标朝向
		return;
	}
	
	TargetYaw = DesiredDirection.Rotation().Yaw;
}

float AExtraPlayerCharacter::GetTargetDelta() const
{
	return FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, TargetYaw);
}

void AExtraPlayerCharacter::LerpArmLength(float Goal)
{
	GetWorldTimerManager().ClearTimer(ArmLengthLerpTimerHandle);
	ArmLengthLerpTimerHandle=GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,&ThisClass::TickArmLengthLerp,Goal));
}

void AExtraPlayerCharacter::TickArmLengthLerp(float Goal)
{
	// 战斗镜头在战斗开始会接管臂长；此时中断已在途的 Zoom 插值，把写入权让出去
	if (CombatCameraComp && CombatCameraComp->IsManagingArmLength())
	{
		return;
	}

	const float CurrentArmLength=CamBoom->TargetArmLength;

	if (FMath::Abs(CurrentArmLength - Goal) < 1.f)
	{
		CamBoom->TargetArmLength=Goal;
		return;
	}

	//递归核心
	const float LerpAlpha=FMath::Clamp(GetWorld()->GetDeltaSeconds() * ZoomLerpSpeed, 0.f, 1.f);
	const float NewArmLength=FMath::Lerp(CurrentArmLength, Goal, LerpAlpha);

	CamBoom->TargetArmLength=NewArmLength;

	ArmLengthLerpTimerHandle=GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this,&ThisClass::TickArmLengthLerp,Goal));
}


void AExtraPlayerCharacter::PlayProxyAction(EMoveReleaseAction Action, float InStartPosition)
{
	switch (Action)
	{
	case EMoveReleaseAction::QuickStopLeft:  PlayQuickStopMontage(true, InStartPosition);  break;
	case EMoveReleaseAction::QuickStopRight: PlayQuickStopMontage(false, InStartPosition); break;
	case EMoveReleaseAction::TurnLeft:       PlayTurnMontage(true, InStartPosition);       break;
	case EMoveReleaseAction::TurnRight:      PlayTurnMontage(false, InStartPosition);      break;
	case EMoveReleaseAction::StopLeft:       PlayStopMontageForMode(true, InStartPosition, false);  break;
	case EMoveReleaseAction::StopRight:      PlayStopMontageForMode(false, InStartPosition, false); break;
	case EMoveReleaseAction::StopWalkLeft:   PlayStopMontageForMode(true, InStartPosition, true);   break;
	case EMoveReleaseAction::StopWalkRight:  PlayStopMontageForMode(false, InStartPosition, true);  break;
	default: break;
	}
}

void AExtraPlayerCharacter::PlayQuickStopMontage(bool bLeft, float InStartPosition)
{
	UAnimMontage* MontageToPlay = bLeft ? LeftQuickStopRunMontage : RightQuickStopRunMontage;
	if (!MontageToPlay)
	{
		return;
	}

	// 目标直接用缓存的绝对朝向 TargetYaw，而不是「当前朝向 + 剩余角差」：
	// 后者会把角色在按键期间已经转掉的角度重复计入，急停会停在偏过的朝向
	const FRotator CurrentRot = GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, TargetYaw, CurrentRot.Roll);

	if (MotionWarpingComp)
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("QuickStopTarget");
		WarpTarget.Location = GetActorLocation();
		WarpTarget.Rotation = TargetRot;
		MotionWarpingComp->AddOrUpdateWarpTarget(WarpTarget);
	}

	// 按指定进度起播：模拟代理要和服务器复制的根运动进度对齐，否则会触发位置修正跳变
	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInst->Montage_Play(MontageToPlay, 1.f, EMontagePlayReturnType::MontageLength, InStartPosition, true);
		AnimInst->OnMontageEnded.RemoveAll(this);
		AnimInst->OnMontageEnded.AddDynamic(this, &AExtraPlayerCharacter::OnStopMontageEnded);
	}
}

void AExtraPlayerCharacter::PlayTurnMontage(bool bTurnLeft, float InStartPosition)
{
	UAnimMontage* MontageToPlay = bTurnLeft ? TurnLeft90Montage : TurnRight90Montage;

	if (!MontageToPlay)
	{
		return;
	}
	
	const FRotator CurrentRot = GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, TargetYaw, CurrentRot.Roll);

	if (MotionWarpingComp)
	{
		FMotionWarpingTarget WarpTarget;
		WarpTarget.Name = FName("TurnTarget");
		WarpTarget.Location = GetActorLocation();
		WarpTarget.Rotation = TargetRot;
		MotionWarpingComp->AddOrUpdateWarpTarget(WarpTarget);
	}

	// 转身 montage 结束（播完/被打断）时清零停步请求与残留速度，
	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInst->Montage_Play(MontageToPlay, 1.f, EMontagePlayReturnType::MontageLength, InStartPosition, true);
		AnimInst->OnMontageEnded.RemoveAll(this);
		AnimInst->OnMontageEnded.AddDynamic(this, &AExtraPlayerCharacter::OnStopMontageEnded);
	}
}

void AExtraPlayerCharacter::PlayStopMontage(bool bLeft, float InStartPosition)
{
	PlayStopMontageForMode(bLeft, InStartPosition, GetWalkMode());
}

void AExtraPlayerCharacter::PlayStopMontageForMode(bool bLeft, float InStartPosition, bool bInWalkMode)
{
	UAnimMontage* MontageToPlay = bInWalkMode
		? (bLeft ? LeftWalkStopMontage : RightWalkStopMontage)
		: (bLeft ? LeftRunStopMontage : RightRunStopMontage);

	if (!MontageToPlay && bInWalkMode)
	{
		// Walk 停步资产还没配时回退到 RunStop，至少保证停步 Montage 能起播
		UE_LOG(LogTemp, Warning, TEXT("[StopMontage] Walk 停步 Montage 未配置，回退 RunStop"));
		MontageToPlay = bLeft ? LeftRunStopMontage : RightRunStopMontage;
	}

	if (!MontageToPlay)
	{
		return;
	}
	
	
	if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInst->Montage_Play(MontageToPlay, 1.f, EMontagePlayReturnType::MontageLength, InStartPosition, true);
		AnimInst->OnMontageEnded.RemoveAll(this);
		AnimInst->OnMontageEnded.AddDynamic(this, &AExtraPlayerCharacter::OnStopMontageEnded);
	}
}

void AExtraPlayerCharacter::OnStopMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != LeftQuickStopRunMontage &&
		Montage != RightQuickStopRunMontage &&
		Montage != TurnLeft90Montage &&
		Montage != TurnRight90Montage &&
		Montage != LeftRunStopMontage &&
		Montage != RightRunStopMontage &&
		Montage != LeftWalkStopMontage &&
		Montage != RightWalkStopMontage)
	{
		return;
	}

	UCharacterMovementComponent* CMC = GetCharacterMovement();
	const float VelBefore = CMC ? CMC->Velocity.Size2D() : -1.f;

	if (UExtraGameAnimInstance* AI = Cast<UExtraGameAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AI->ClearStopRequest();

		// 只在正常播完时清零残留速度。
		// 重新移动时保持当前残余速度并叠加新输入的速度
		if (!bInterrupted)
		{
			if (CMC)
			{
				CMC->Velocity = FVector::ZeroVector;
			}
		}
	}
}

void AExtraPlayerCharacter::Server_SendInputDirection_Implementation(FVector InputDir)
{
	InputDirection = InputDir;
}

void AExtraPlayerCharacter::CancelStopMontageIfPlaying()
{
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// 清掉停步请求。
	if (UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(AnimInst))
	{
		GameAI->ClearStopRequest();
	}
	
	const UAnimMontage* StopMontages[] = {
		LeftQuickStopRunMontage, RightQuickStopRunMontage,
		TurnLeft90Montage, TurnRight90Montage,
		LeftRunStopMontage, RightRunStopMontage,
		LeftWalkStopMontage, RightWalkStopMontage };

	FString StoppedNames;
	for (const UAnimMontage* StopMontage : StopMontages)
	{
		if (StopMontage && AnimInst->Montage_IsPlaying(StopMontage))
		{
			if (!StoppedNames.IsEmpty())
			{
				StoppedNames += TEXT(",");
			}
			StoppedNames += StopMontage->GetName();

			AnimInst->Montage_StopWithBlendOut(StopMontage->BlendOut, StopMontage);
		}
	}
}

void AExtraPlayerCharacter::OnNormalAttackStarted(const FInputActionValue& InputActionValue)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	bHoldingAttack = true;
	Server_SetHoldingAttack(true);
	
	bLongPressed = false;

	// 启动重击长按阈值定时器：按住达到 HeavyAttackHoldTime 才进入「长按重击」判定
	GetWorldTimerManager().SetTimer(
		HeavyAttackHoldTimerHandle,
		this,
		&ThisClass::OnReachHeavyThreshold,
		HeavyAttackHoldTime,
		false);

	// 按下即发送轻击输入。
	// 空中按形态分派：一阶段直接打空中攻击，二阶段交给二阶段空中连段GA，
	FGameplayTag AttackTag = UUExtraAbilitySystemStatic::GetLightAttackInputTag();
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling()
		&& !AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase2StateTag()))
	{
		AttackTag = UUExtraAbilitySystemStatic::GetAirDiveInputTag();
	}

	AbilitySystemComponent->AbilityInputTagPressed(AttackTag);
}

void AExtraPlayerCharacter::OnNormalAttackCompleted(const FInputActionValue& InputActionValue)
{
	bHoldingAttack = false;
	bLongPressed = false;

	// 与按下时对称：松开同样要上行，否则服务端会一直以为按着，自动续段停不下来
	Server_SetHoldingAttack(false);
	
	if (GetWorldTimerManager().IsTimerActive(HeavyAttackHoldTimerHandle))
	{
		GetWorldTimerManager().ClearTimer(HeavyAttackHoldTimerHandle);
	}

	// 松手：让监听输入松开的能力收到 release（蓄力重击靠它打出结束段）。
	// 轻击与重击共用同一个物理键，两个都松开
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityInputTagReleased(UUExtraAbilitySystemStatic::GetHeavyAttackInputTag());
		AbilitySystemComponent->AbilityInputTagReleased(UUExtraAbilitySystemStatic::GetLightAttackInputTag());
	}
}

void AExtraPlayerCharacter::OnReachHeavyThreshold()
{
	bLongPressed = true;

	if (!AbilitySystemComponent)
	{
		return;
	}

	// 二阶段：纯长按达阈值即触发重击，是否真正激活由 GA 的 State.Phase2 门控裁决
	if (AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase2StateTag()))
	{
		// 攻击强化就绪时改触发 GA_AttackPro_Phase_2。走专属 Tag 分流：
		const bool bAttackProReady =
			AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetProReadyTag());

		// 长按达阈值：等同一次重击输入。这里在定时器回调里，已错过本帧的 ProcessAbilityInput，手动补一次
		AbilitySystemComponent->AbilityInputTagPressed(
			bAttackProReady
				? UUExtraAbilitySystemStatic::GetAttackProInputTag()
				: UUExtraAbilitySystemStatic::GetHeavyAttackInputTag());
		AbilitySystemComponent->ProcessAbilityInput();
		return;
	}

	// 一阶段：需打满能量才触发重击
	if (AbilitySystemComponent->HasMatchingGameplayTag(UUExtraAbilitySystemStatic::GetPhase1StateTag()))
	{
		const float EnergyValue = AbilitySystemComponent->GetNumericAttribute(UExtraGameAttributeSet::GetEnergyValueAttribute());
		if (EnergyValue >= GetHeavyComboEnergyNeed())
		{
			AbilitySystemComponent->AbilityInputTagPressed(UUExtraAbilitySystemStatic::GetHeavyAttackInputTag());
			AbilitySystemComponent->ProcessAbilityInput();
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

bool AExtraPlayerCharacter::ConsumeSkill01ComboBoost()
{
	const bool bPending = bSkill01ComboBoost;
	bSkill01ComboBoost = false;
	return bPending;
}

void AExtraPlayerCharacter::OnSkillStarted(const FInputActionValue& InputActionValue)
{
	// 按住状态：长按到技能 Montage 的居合检测帧有条件进入居合
	bHoldingSkill = true;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityInputTagPressed(UUExtraAbilitySystemStatic::GetSkillInputTag());
	}
}

void AExtraPlayerCharacter::OnSkillCompleted(const FInputActionValue& InputActionValue)
{
	bHoldingSkill = false;
}

void AExtraPlayerCharacter::OnUltimateStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityInputTagPressed(UUExtraAbilitySystemStatic::GetUltimateInputTag());
	}
}

void AExtraPlayerCharacter::OnDodgeStarted(const FInputActionValue& InputActionValue)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AbilityInputTagPressed(UUExtraAbilitySystemStatic::GetDodgeInputTag());
	}
}