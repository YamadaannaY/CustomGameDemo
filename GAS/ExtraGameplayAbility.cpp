#include "ExtraGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"

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
	//因为移动CancelGA时，手动停Montage
	if (bEndingFromMovement)
	{
		if (UAnimMontage* ActiveMontage = GetActiveMontageForCancel())
		{
			UAnimInstance* AnimInst = GetOwnerAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(ActiveMontage))
			{ 
				//采用默认BlendOut
				AnimInst->Montage_StopWithBlendOut(ActiveMontage->BlendOut, ActiveMontage);
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
	
	// 恢复重力缩放,恢复到引擎默认，而非激活前那一刻的值。
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

	// 兜底清理 CancelWindow：蒙太奇被异常掐断时 NotifyEnd 不会到达，这里恢复封锁并解除登记
	if (bEnableCancelWindow)
	{
		ExitCancelWindow();
	}

	// 解绑 MW 每帧回调并移除已注册的朝向 warp target，避免 GA 结束后残留
	if (Char && Char->GetMotionWarpingComponent())
	{
		Char->GetMotionWarpingComponent()->OnPreUpdate.RemoveDynamic(this, &ThisClass::OnMotionWarpingPreUpdate);
		Char->GetMotionWarpingComponent()->RemoveWarpTarget(LockOnWarpTargetName);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UExtraGameplayAbility::CommitAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, FGameplayTagContainer* OptionalRelevantTags)
{
	if (!Super::CommitAbility(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags))
	{
		return false;
	}

	// CancelWindow 入站取消：窗口内的GA已「视为结束」，任何 GA 提交成功即打断它。
	if (bCanInterruptCancelWindow)
	{
		if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
		{
			const FGameplayAbilitySpecHandle Holder = ASC->GetCancelWindowHolder();
			if (Holder.IsValid() && Holder != Handle)
			{
				ASC->CancelAbilityHandle(Holder);
			}
		}
	}

	return true;
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

	// 后摇可打断窗口：监听 ANS_CancelWindow 的开/关窗事件
	if (bEnableCancelWindow)
	{
		SetupCancelWindowListener();
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

	// 角色中心范围伤害：开启 bEnableAreaDamage 后，服务端监听范围伤害触发事件并做半径判定。
	if (bEnableAreaDamage && K2_HasAuthority())
	{
		SetupAreaDamageListener();
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
					
					//第一个激活GA缓存一次即可
					bGravityDefaultCached = true;
				}

				Movement->GravityScale = AbilityGravityScale;
			}
		}
	}

	// 攻击朝向（MR）：激活即写入 warp target，并挂上 MW 的每帧回调持续同步
	// （跟随目标移动 / 跟随输入方向 / 无输入时不再干涉）。
	// 仅攻击 GA 开启（bRotateToLockTarget），ActivateAbility 阶段播放的 Montage 由动画内 MR 区间完成转向。
	if (bRotateToLockTarget)
	{
		if (AExtraPlayerCharacter* PlayerChar = GetOwningAvatarCharacter())
		{
			if (UMotionWarpingComponent* MWC = PlayerChar->GetMotionWarpingComponent())
			{
				MWC->OnPreUpdate.AddDynamic(this, &ThisClass::OnMotionWarpingPreUpdate);
			}
		}

		UpdateLockOnWarpTarget();
	}
}

void UExtraGameplayAbility::SetupMovementCancel()
{
	//重置标记
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
		this, UUExtraAbilitySystemStatic::GetUninterruptibleEndTag(), nullptr, false, true );
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
		this, GetDamageEventTag(), nullptr,false,true);
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

FGameplayTag UExtraGameplayAbility::GetAreaDamageTriggerTag() const
{
	return UUExtraAbilitySystemStatic::GetAreaDamageTag();
}

void UExtraGameplayAbility::SetupAreaDamageListener()
{
	// OnlyTriggerOnce=false：一段 Montage 内多个伤害帧（多个 AN）都要响应
	UAbilityTask_WaitGameplayEvent* WaitAreaTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, GetAreaDamageTriggerTag(), nullptr,false,true);
	WaitAreaTask->EventReceived.AddDynamic(this, &ThisClass::OnAreaDamageEventReceived);
	WaitAreaTask->ReadyForActivation();
}

void UExtraGameplayAbility::OnAreaDamageEventReceived(FGameplayEventData Payload)
{
	// 解析 AN 提供的圆心参数与半径覆写
	FVector CenterOffset = FVector::ZeroVector;
	float Radius = 0.f;
	EAreaCenterMode CenterMode = EAreaCenterMode::Inherit;

	const FGameplayAbilityTargetDataHandle& Handle = Payload.TargetData;
	for (int32 i = 0; i < Handle.Num(); ++i)
	{
		const FGameplayAbilityTargetData* Data = Handle.Get(i);
		if (Data && Data->GetScriptStruct() == FAreaCheckData::StaticStruct())
		{
			const FAreaCheckData* AreaData = static_cast<const FAreaCheckData*>(Data);
			CenterOffset = AreaData->CenterOffset;
			Radius = AreaData->Radius;
			CenterMode = AreaData->CenterMode;
			break;
		}
	}

	PerformAreaDamage(CenterOffset, Radius, CenterMode);
}

void UExtraGameplayAbility::PerformAreaDamage(const FVector& CenterOffset, float Radius, EAreaCenterMode CenterMode)
{
	// 伤害判定只在服务端执行
	if (!K2_HasAuthority())
	{
		return;
	}

	AExtraCharacter* Char = Cast<AExtraCharacter>(GetAvatarActorFromActorInfo());
	UWorld* World = GetWorld();
	if (!Char || !World)
	{
		return;
	}

	// AN 未指定半径时沿用 GA 自身配置
	if (Radius <= 0.f)
	{
		Radius = AreaDamageRadius;
	}
	if (Radius <= 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ExtraGameplayAbility] PerformAreaDamage: 半径 <= 0（AN 与 GA 均未配置），skip."));
		return;
	}

	// AN 未指定圆心来源（Inherit）时用 GA 配置解析
	if (CenterMode == EAreaCenterMode::Inherit)
	{
		CenterMode = bAreaDamageUseLockTargetAsCenter ? EAreaCenterMode::LockTarget : EAreaCenterMode::Owner;
	}

	// 圆心：LockTarget 且当前有锁定目标时取目标位置（忽略 CenterOffset）；否则角色位置 + XY 偏移（Z 沿用角色高度）
	FVector Center = Char->GetActorLocation();
	bool bCenterOnLockTarget = false;
	if (CenterMode == EAreaCenterMode::LockTarget)
	{
		if (const AExtraPlayerCharacter* PlayerChar = GetOwningAvatarCharacter())
		{
			if (const AActor* LockTarget = PlayerChar->GetLockTarget())
			{
				Center = LockTarget->GetActorLocation();
				bCenterOnLockTarget = true;
			}
		}
	}

	if (!bCenterOnLockTarget)
	{
		Center.X += CenterOffset.X;
		Center.Y += CenterOffset.Y;
	}

	// 收集半径内敌方存活单位（与 HeavyAttack 时停判定同口径：不同 Team + Health>0）
	TArray<AActor*> Targets;
	for (TActorIterator<AExtraCharacter> It(World); It; ++It)
	{
		AExtraCharacter* Enemy = *It;
		if (!Enemy || Enemy == Char)
		{
			continue;
		}
		if (Enemy->GetGenericTeamId() == Char->GetGenericTeamId())
		{
			continue;
		}

		const UAbilitySystemComponent* EnemyASC = Enemy->GetAbilitySystemComponent();
		if (!EnemyASC || EnemyASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute()) <= 0.f)
		{
			continue;
		}

		if (FVector::DistSquared(Center, Enemy->GetActorLocation()) > FMath::Square(Radius))
		{
			continue;
		}

		Targets.Add(Enemy);
	}

	if (bShouldDrawDebug)
	{
		DrawAreaDamageDebug(Center, Radius, Targets);
	}

	if (Targets.Num() == 0)
	{
		return;
	}

	// 统一结算：包成 TargetData 交给基类 DoDamage，对所有目标应用同一个伤害 GE（GetDamageEffect 可选）
	FGameplayAbilityTargetData_ActorArray* ActorArray = new FGameplayAbilityTargetData_ActorArray();
	for (AActor* Target : Targets)
	{
		ActorArray->TargetActorArray.Add(Target);
	}

	FGameplayEventData EventData;
	EventData.Instigator = Char;
	EventData.TargetData.Add(ActorArray);
	DoDamage(EventData);
}

void UExtraGameplayAbility::DrawAreaDamageDebug(const FVector& Center, float Radius, const TArray<AActor*>& Targets)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	Radius = FMath::Max(Radius, 1.f);
	const float LifeTime = 4.f;
	const FColor RangeColor = Targets.Num() > 0 ? FColor::Green : FColor::Red;

	// 地面脚印圈：与实际判定同半径，俯视/平视即可目测波及范围
	AExtraCharacter* Char = Cast<AExtraCharacter>(GetAvatarActorFromActorInfo());
	FVector GroundCenter = Center;
	if (Char && Char->GetCapsuleComponent())
	{
		GroundCenter.Z -= Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2.f;
	}

	//绘制地面圆
	constexpr int32 Segments = 32;
	FVector Prev = GroundCenter + FVector(Radius, 0.f, 0.f);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = 2.f * PI * static_cast<float>(i) / static_cast<float>(Segments);
		const FVector Curr = GroundCenter + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		DrawDebugLine(World, Prev, Curr, RangeColor, false, LifeTime);
		Prev = Curr;
	}

	// 判定球体
	DrawDebugSphere(World, Center, Radius, 16, RangeColor, false, LifeTime);

	// 命中目标：连线 + 打点
	for (const AActor* Target : Targets)
	{
		if (!Target)
		{
			continue;
		}
		const FVector TargetLoc = Target->GetActorLocation();
		DrawDebugLine(World, Center + FVector(0.f, 0.f, 20.f), TargetLoc + FVector(0.f, 0.f, 20.f), FColor::Orange, false, LifeTime);
		DrawDebugSphere(World, TargetLoc, 25.f, 8, FColor::Orange, false, LifeTime);
	}

	if (GEngine)
	{
		const FString Msg = FString::Printf(TEXT("[AreaDamage] Radius=%.0f cm | EnemiesInRange=%d"), Radius, Targets.Num());
		GEngine->AddOnScreenDebugMessage(-1, LifeTime, RangeColor, Msg);
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

void UExtraGameplayAbility::SetupCancelWindowListener()
{
	bInCancelWindow = false;

	// OnlyMatchExact=true：只接住 AN 发的这两个精确 tag
	UAbilityTask_WaitGameplayEvent* WaitBeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetCancelWindowBeginTag(), nullptr, false, true);
	WaitBeginTask->EventReceived.AddDynamic(this, &ThisClass::OnCancelWindowBeginReceived);
	WaitBeginTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* WaitEndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetCancelWindowEndTag(), nullptr, false, true);
	WaitEndTask->EventReceived.AddDynamic(this, &ThisClass::OnCancelWindowEndReceived);
	WaitEndTask->ReadyForActivation();
}

void UExtraGameplayAbility::OnCancelWindowBeginReceived(FGameplayEventData Payload)
{
	EnterCancelWindow();
}

void UExtraGameplayAbility::OnCancelWindowEndReceived(FGameplayEventData Payload)
{
	ExitCancelWindow();
}

void UExtraGameplayAbility::EnterCancelWindow()
{
	if (bInCancelWindow)
	{
		return;
	}
	bInCancelWindow = true;

	// 「视为已取消」第一步：撤销表现段的封锁。
	SetShouldBlockOtherAbilities(false);

	// 「视为已取消」第二步：登记自己为可被任意 GA 取消的持有者，其他GA进行Commit时会Cancel窗口内（即持有WindowState）的GA
	if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->SetCancelWindowHolder(GetCurrentAbilitySpecHandle());
		ASC->AddLooseGameplayTag(UUExtraAbilitySystemStatic::GetCancelWindowStateTag());
	}
}

void UExtraGameplayAbility::ExitCancelWindow()
{
	if (!bInCancelWindow)
	{
		return;
	}
	bInCancelWindow = false;

	// 恢复封锁（窗口结束但 GA 仍在播后续段时必须恢复，否则之后永远挡不住同类 GA）
	SetShouldBlockOtherAbilities(true);

	if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->ClearCancelWindowHolder(GetCurrentAbilitySpecHandle());
		ASC->RemoveLooseGameplayTag(UUExtraAbilitySystemStatic::GetCancelWindowStateTag());
	}
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
	if (!MWC)
	{
		return;
	}

	// 三态：有锁定目标 → 位移+旋转都 warp；无目标有输入 → 仅旋转；无目标无输入 → 都不干涉。
	const AActor* LockTarget = PlayerChar->GetLockTarget();

	FVector FaceDir = FVector::ZeroVector;
	FVector WarpLocation = PlayerChar->GetActorLocation();
	bool bWarpTranslation = false;
	bool bWarpRotation = false;

	if (LockTarget)
	{
		// 水平化：只旋转 Yaw 面向目标，不改变俯仰（角色保持水平站立）
		FVector FlatDir = LockTarget->GetActorLocation() - PlayerChar->GetActorLocation();
		FlatDir.Z = 0.f;
		if (FlatDir.IsNearlyZero())
		{
			return;
		}

		// 落点与「是否位移 warp」交给虚函数：默认落在目标位置，居合前冲覆写为穿过目标落在身后
		WarpLocation = ComputeLockOnWarpLocation(PlayerChar, LockTarget, FlatDir.GetSafeNormal(), bWarpTranslation);

		// 朝向同样交给虚函数：默认朝目标，居合前冲覆写为锁定起手方向
		FaceDir = ComputeLockOnFaceDir(PlayerChar, LockTarget, FlatDir.GetSafeNormal());
		bWarpRotation = true;
	}
	else if (bRotateToInputWhenNoTarget && !PlayerChar->GetInputDirection().IsNearlyZero())
	{
		// 无目标但有移动输入：只把朝向拧到输入方向，位移交给动画自身的根位移
		FaceDir = PlayerChar->GetInputDirection();
		FaceDir.Z = 0.f;
		if (FaceDir.IsNearlyZero())
		{
			return;
		}

		bWarpRotation = true;
	}
	else
	{
		// 无目标且无输入：不干涉根运动（等价于没有该 MW）。
		FaceDir = PlayerChar->GetActorForwardVector();
	}
	
	
	FMotionWarpingTarget WarpTarget;
	WarpTarget.Name = LockOnWarpTargetName;
	WarpTarget.Location = WarpLocation;
	WarpTarget.Rotation = FRotationMatrix::MakeFromX(FaceDir).Rotator();

	MWC->AddOrUpdateWarpTarget(WarpTarget);

	// 位移/旋转开关只存在于 modifier 上，且每次 NMS 区间开始都会重建 modifier、
	// 把开关拷回默认 true，因此每帧按当前状态重设。
	for (URootMotionModifier* Mod : MWC->GetModifiers())
	{
		URootMotionModifier_Warp* WarpMod = Cast<URootMotionModifier_Warp>(Mod);
		if (WarpMod && WarpMod->WarpTargetName == LockOnWarpTargetName)
		{
			WarpMod->bWarpTranslation = bWarpTranslation;
			WarpMod->bWarpRotation = bWarpRotation;
		}
	}
}

FVector UExtraGameplayAbility::ComputeLockOnWarpLocation(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget, bool& bOutWarpTranslation) const
{
	bOutWarpTranslation = true;

	if (!PlayerChar || !LockTarget)
	{
		return PlayerChar ? PlayerChar->GetActorLocation() : FVector::ZeroVector;
	}

	// 有限MW追踪：距离不超过上限时 warp 落点在目标身上；超出时把落点钳制到自身朝目标的MotionWarpMaxMoveDist位置，避免动画强制位移超出设定距离。
	const float DistanceToTarget = FVector::Dist2D(LockTarget->GetActorLocation(), PlayerChar->GetActorLocation());
	if (DistanceToTarget > MotionWarpMaxMoveDist)
	{
		return PlayerChar->GetActorLocation() + DirToTarget * MotionWarpMaxMoveDist - 20.f;
	}

	return LockTarget->GetActorLocation();
}

FVector UExtraGameplayAbility::ComputeLockOnFaceDir(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget) const
{
	// 默认朝目标
	return DirToTarget;
}

void UExtraGameplayAbility::OnMotionWarpingPreUpdate(UMotionWarpingComponent* MotionWarpingComp)
{
	UpdateLockOnWarpTarget();
}

AExtraPlayerCharacter* UExtraGameplayAbility::GetOwningAvatarCharacter()
{
	if (!AvatarCharacter)
	{
		AvatarCharacter=Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	}
	return AvatarCharacter;
}
