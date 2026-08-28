#include "ExtraGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

AExtraPlayerCharacter* UExtraGameplayAbility::GetOwningAvatarCharacter()
{
	if (!AvatarCharacter)
	{
		AvatarCharacter=Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	}
	return AvatarCharacter;
}
