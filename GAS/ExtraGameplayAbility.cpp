#include "ExtraGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UExtraGameplayAbility::UExtraGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;

	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetUninterruptibleTag());
	
	UninterruptibleTag = UUExtraAbilitySystemStatic::GetUninterruptibleTag();
}

UAnimInstance* UExtraGameplayAbility::GetOwnerAnimInstance() const
{
	USkeletalMeshComponent* OwnerSkeletalMeshComp=GetOwningComponentFromActorInfo();
	if (OwnerSkeletalMeshComp)
	{
		return OwnerSkeletalMeshComp->GetAnimInstance();
	}
	return nullptr;
}

void UExtraGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	//Cancel机制取消GA时，手动停Montage
	if (bEndingFromMovement)
	{
		if (UAnimMontage* ActiveMontage = GetActiveMontageForCancel())
		{
			UAnimInstance* AnimInst = GetOwnerAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(ActiveMontage))
			{
				AnimInst->Montage_Stop(MontageCancelBlendOutTime, ActiveMontage);
			}
		}
	}

	AExtraPlayerCharacter* Char = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (Char && Char->GetWeaponComponent() && ClearWeaponShowOnAbilityEnd)
	{
		Char->GetWeaponComponent()->HideWeapon();
	}

	// 兜底：即使蒙太奇异常终止未触发 ANS 的 NotifyEnd，GA 结束也强制关闭轨迹扫描窗口（幂等）
	if (Char && Char->GetWeaponComponent())
	{
		Char->GetWeaponComponent()->EndWeaponTrace();
	}
	
	// 恢复重力缩放（若本次激活启用了重力缩放）：永远恢复到引擎默认，而非激活前那一刻的值。
	if (bEnableGravityScale)
	{
		if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			if (UCharacterMovementComponent* Movement = AvatarChar->GetCharacterMovement())
			{
				Movement->GravityScale = DefaultGravityScale;
			}
		}
	}

	// 兜底清理霸体 tag
	if (bEnableUninterruptible)
	{
		ReleaseUninterruptible();
	}

	// 清理锁定转向刷新定时器（激活失败 / 取消 / 正常结束统一走这里）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LockOnWarpRefreshTimerHandle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UExtraGameplayAbility::PreActivate(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData)
{
	Super::PreActivate(Handle, ActorInfo, ActivationInfo, OnGameplayAbilityEndedDelegate, TriggerEventData);

	// 移动打断在此统一监听。由AN_Cancel在动画帧发送
	if (bEnableMovementCancel)
	{
		SetupMovementCancel();
	}

	// 推力 任何 GA 激活期间统一监听 Push_Self 事件，由 AN_ApplyPush 在动画帧发送。
	SetupPushSelfListener();

	// 霸体窗口：激活即挂载 State.Uninterruptible，后摇段由 AN_EndUninterruptible 发送事件放开。
	if (bEnableUninterruptible)
	{
		ApplyUninterruptibleTag();
		SetupUninterruptibleReleaseListener();
	}

	// 通用武器碰撞伤害：攻击 GA 开启WeaponDamage选项后，服务端监听Trace命中事件并应用DamageGE。
	if (bEnableWeaponDamage && K2_HasAuthority())
	{
		SetupDamageListener();
	}

	// 重力缩放：激活时缓存引擎默认重力（仅首次）并应用 AbilityGravityScale，EndAbility 统一恢复默认。
	if (bEnableGravityScale)
	{
		if (ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			if (UCharacterMovementComponent* Movement = AvatarChar->GetCharacterMovement())
			{
				if (!bGravityDefaultCached)
				{
					if (const UCharacterMovementComponent* DefaultMovement = Movement->GetClass()->GetDefaultObject<UCharacterMovementComponent>())
					{
						DefaultGravityScale = DefaultMovement->GravityScale;
					}
					bGravityDefaultCached = true;
				}

				Movement->GravityScale = AbilityGravityScale;
			}
		}
	}

	// 锁定目标转向（MR）：激活即写入朝向 warp target，并周期刷新跟随目标移动。
	// 仅攻击 GA 开启（bRotateToLockTarget），ActivateAbility 阶段播放的 Montage 由动画内 MR 区间完成转向。
	if (bRotateToLockTarget)
	{
		UpdateLockOnWarpTarget();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				LockOnWarpRefreshTimerHandle,
				this,
				&ThisClass::UpdateLockOnWarpTarget,
				LockOnWarpRefreshInterval,
				true);
		}
	}
}

void UExtraGameplayAbility::SetupMovementCancel()
{
	// 重置跨激活状态为PerActorGA实例复用
	bEndingFromMovement = false;

	UAbilityTask_WaitGameplayEvent* WaitCancelTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, GetMovementCancelTag());
	WaitCancelTask->EventReceived.AddDynamic(this, &ThisClass::OnMovementCancelNotifyReceived);
	WaitCancelTask->ReadyForActivation();
}

void UExtraGameplayAbility::SetupPushSelfListener()
{
	UAbilityTask_WaitGameplayEvent* WaitPushTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, UUExtraAbilitySystemStatic::GetPushSelfTag(), nullptr, false, false);
	WaitPushTask->EventReceived.AddDynamic(this, &ThisClass::OnPushSelfNotifyReceived);
	WaitPushTask->ReadyForActivation();
}

void UExtraGameplayAbility::ApplyUninterruptibleTag()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	ASC->AddLooseGameplayTag(UninterruptibleTag);
	bUninterruptibleActive = true;
}

void UExtraGameplayAbility::ReleaseUninterruptible()
{
	if (!bUninterruptibleActive)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	ASC->RemoveLooseGameplayTag(UninterruptibleTag);
	bUninterruptibleActive = false;
}

void UExtraGameplayAbility::SetupUninterruptibleReleaseListener()
{
	UAbilityTask_WaitGameplayEvent* WaitUninterruptibleTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetUninterruptibleEndTag(), nullptr, false, false);
	WaitUninterruptibleTask->EventReceived.AddDynamic(this, &ThisClass::OnUninterruptibleReleaseReceived);
	WaitUninterruptibleTask->ReadyForActivation();
}

void UExtraGameplayAbility::OnUninterruptibleReleaseReceived(FGameplayEventData Payload)
{
	ReleaseUninterruptible();
}

void UExtraGameplayAbility::SetupDamageListener()
{
	// OnlyTriggerOnce=false：一个攻击窗口命中多个目标、或一个蒙太奇含多个窗口时都要响应
	UAbilityTask_WaitGameplayEvent* WaitDamageTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, GetDamageEventTag(), nullptr, /*OnlyTriggerOnce*/ false, /*OnlyMatchExact*/ true);
	WaitDamageTask->EventReceived.AddDynamic(this, &ThisClass::OnDamageEventReceived);
	WaitDamageTask->ReadyForActivation();
}

FGameplayTag UExtraGameplayAbility::GetDamageEventTag() const
{
	return UUExtraAbilitySystemStatic::GetAbilityDamageEventTag();
}

TSubclassOf<UGameplayEffect> UExtraGameplayAbility::GetDamageEffect() const
{
	return DefaultWeaponDamageEffect;
}

void UExtraGameplayAbility::OnDamageEventReceived(FGameplayEventData Data)
{
	DoDamage(Data);
}

void UExtraGameplayAbility::DoDamage(const FGameplayEventData& Data)
{
	// 伤害判定只在服务端执行（来源：武器轨迹扫描经 GameplayEvent 发送的 TargetData）
	if (!K2_HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!SourceASC)
	{
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	const TSubclassOf<UGameplayEffect> DamageEffect = GetDamageEffect();
	if (!DamageEffect)
	{
		return;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Avatar, Avatar);
	Context.AddSourceObject(Avatar);
	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffect, GetAbilityLevel(), Context);

	// 伤害数值由 GE 自身配置（字面量 / AttributeBased / Execution），不在此注入 SetByCaller
	const TArray<AActor*> HitActors = UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(Data.TargetData);

	for (AActor* HitActor : HitActors)
	{
		UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	}
}

void UExtraGameplayAbility::OnPushSelfNotifyReceived(FGameplayEventData Payload)
{
	FVector PushVelocity = FVector::ZeroVector;
	bool bOverrideXY = false;
	bool bOverrideZ = true;

	const FGameplayAbilityTargetDataHandle& Handle = Payload.TargetData;
	for (int32 i = 0; i < Handle.Num(); ++i)
	{
		const FGameplayAbilityTargetData* Data = Handle.Get(i);
		if (Data && Data->GetScriptStruct() == FPushTargetData::StaticStruct())
		{
			const FPushTargetData* PushData = static_cast<const FPushTargetData*>(Data);
			PushVelocity = PushData->PushVelocity;
			bOverrideXY = PushData->bOverrideXY;
			bOverrideZ = PushData->bOverrideZ;
			break;
		}
	}

	PushSelf(PushVelocity, bOverrideXY, bOverrideZ);
}

void UExtraGameplayAbility::PushSelf(const FVector& PushVel, bool bOverrideXY, bool bOverrideZ)
{
	if (ACharacter* OwningAvatarCharacter=GetOwningAvatarCharacter())
	{
		OwningAvatarCharacter->LaunchCharacter(PushVel,bOverrideXY,bOverrideZ);
	}
}

void UExtraGameplayAbility::PushTarget(AActor* Target, const FVector& PushVel)
{
	if (!Target) return;
	
	FGameplayEventData EventData;

	FGameplayAbilityTargetData_SingleTargetHit* HitData=new FGameplayAbilityTargetData_SingleTargetHit;
	FHitResult HitResult;
	HitResult.ImpactNormal=PushVel;
	
	HitData->HitResult=HitResult;
	EventData.TargetData.Add(HitData);

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Target,UUExtraAbilitySystemStatic::GetLaunchedAbilityActivationTag(),EventData);
}

void UExtraGameplayAbility::PushTargets(const TArray<AActor*>& Targets, const FVector PushVel)
{
	for (AActor* Target : Targets)
	{
		PushTarget(Target,PushVel);
	}
}

void UExtraGameplayAbility::PushTargets(const FGameplayAbilityTargetDataHandle& TargetDataHandle,const FVector& PushVel)
{
	TArray<AActor*> Targets=UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(TargetDataHandle);
	PushTargets(Targets,PushVel);
}

void UExtraGameplayAbility::PushTargetsFromLocation(const FGameplayAbilityTargetDataHandle& TargetDataHandle,const FVector& FromLocation, float PushSpeed)
{
	const TArray<AActor*> Targets=UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(TargetDataHandle);

	PushTargetsFromLocation(Targets,FromLocation,PushSpeed);
}

void UExtraGameplayAbility::PushTargetsFromOwnerLocation(const TArray<AActor*>& Targets, float PushSpeed)
{
	AActor* OwnerAvatarActor = GetAvatarActorFromActorInfo();
	if (!OwnerAvatarActor)	return;

	FVector OwnerAvatarActorLocation = OwnerAvatarActor->GetActorLocation();
	PushTargetsFromLocation(Targets, OwnerAvatarActorLocation, PushSpeed);
}

void UExtraGameplayAbility::PushTargetsFromLocation(const TArray<AActor*>& Targets, const FVector& FromLocation,float PushSpeed)
{
	for (AActor* Target : Targets)
	{
		FVector PushDir = Target->GetActorLocation() - FromLocation;
		PushDir.Z = 0;
		PushDir.Normalize();

		PushTarget(Target , PushDir*PushSpeed);
	}
}

FGameplayTag UExtraGameplayAbility::GetMovementCancelTag() const
{
	return UUExtraAbilitySystemStatic::GetAbilityCancelTag();
}

void UExtraGameplayAbility::OnMovementCancelNotifyReceived(FGameplayEventData Payload)
{
	// 事件由 AN_CancelWindow 在区间内、且已检测到移动输入时发送，到达即打断。
	bEndingFromMovement = true;

	const FString GAName = GetClass()->GetName();
	UE_LOG(LogTemp, Log, TEXT("[MovementCancel] %s GA 被取消"), *GAName);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("%s GA 被取消"), *GAName));
	}

	OnMovementCancelTriggered();
	
	K2_EndAbility();
}

void UExtraGameplayAbility::UpdateLockOnWarpTarget()
{
	AExtraPlayerCharacter* PlayerChar = GetOwningAvatarCharacter();
	if (!PlayerChar)
	{
		return;
	}

	// warp target 仅本机设置（本地控制），避免服务端重复写入 / 客户端双重转向
	const APawn* Pawn = Cast<APawn>(PlayerChar);
	if (Pawn && !Pawn->IsLocallyControlled())
	{
		return;
	}

	UMotionWarpingComponent* MWC = PlayerChar->GetMotionWarpingComponent();
	const AActor* LockTarget = PlayerChar->GetLockTarget();
	if (!MWC || !LockTarget)
	{
		return;
	}

	// 水平化：只旋转 Yaw 面向目标，不改变俯仰（角色保持水平站立）
	FVector FlatDir = LockTarget->GetActorLocation() - PlayerChar->GetActorLocation();
	FlatDir.Z = 0.f;
	if (FlatDir.IsNearlyZero())
	{
		return;
	}

	FMotionWarpingTarget WarpTarget;
	WarpTarget.Name = LockOnWarpTargetName;
	WarpTarget.Location = PlayerChar->GetActorLocation();
	WarpTarget.Rotation = FRotationMatrix::MakeFromX(FlatDir).Rotator();

	MWC->AddOrUpdateWarpTarget(WarpTarget);
}

AExtraPlayerCharacter* UExtraGameplayAbility::GetOwningAvatarCharacter()
{
	if (!AvatarCharacter)
	{
		AvatarCharacter=Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	}
	return AvatarCharacter;
}
