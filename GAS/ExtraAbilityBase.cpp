#include "ExtraAbilityBase.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"

UExtraAbilityBase::UExtraAbilityBase()
{
	// 霸体期间（含自己）一律不能被激活：表现段不该被受击等反应打断。
	// 需要例外时派生类自行从 ActivationBlockedTags 中移除该 tag
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetUninterruptibleTag());

	UninterruptibleTag = UUExtraAbilitySystemStatic::GetUninterruptibleTag();
}

void UExtraAbilityBase::PreActivate(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData)
{
	Super::PreActivate(Handle, ActorInfo, ActivationInfo, OnGameplayAbilityEndedDelegate, TriggerEventData);

	// 后摇可打断窗口：监听 AN_CancelWindow 的开/关窗事件
	if (bEnableCancelWindow)
	{
		SetupCancelWindowListener();
	}

	// 推力：任何 GA 激活期间统一监听 Push_Self 事件，由 Montage 里的 AN_ApplyPush 在动画帧发送
	SetupPushSelfListener();

	// 霸体窗口：激活即挂载 State.Uninterruptible，后摇段由 AN_EndUninterruptible 发送事件放开
	if (bEnableUninterruptible)
	{
		ApplyUninterruptibleTag();
		SetupUninterruptibleReleaseListener();
	}
}

void UExtraAbilityBase::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 兜底清理霸体 tag
	if (bEnableUninterruptible)
	{
		ReleaseUninterruptible();
	}

	// 兜底清理 CancelWindow：恢复封锁并解除登记
	if (bEnableCancelWindow)
	{
		ExitCancelWindow();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UExtraAbilityBase::CommitAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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

// ── 霸体窗口 ────────────────────────────────────────────

void UExtraAbilityBase::ApplyUninterruptibleTag()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	ASC->AddLooseGameplayTag(UninterruptibleTag);
	bUninterruptibleActive = true;
}

void UExtraAbilityBase::ReleaseUninterruptible()
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

void UExtraAbilityBase::SetupUninterruptibleReleaseListener()
{
	UAbilityTask_WaitGameplayEvent* WaitUninterruptibleTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetUninterruptibleEndTag(), nullptr, false, true );
	WaitUninterruptibleTask->EventReceived.AddDynamic(this, &ThisClass::OnUninterruptibleReleaseReceived);
	WaitUninterruptibleTask->ReadyForActivation();
}

void UExtraAbilityBase::OnUninterruptibleReleaseReceived(FGameplayEventData Payload)
{
	ReleaseUninterruptible();
}

// ── 后摇可打断窗口（CancelWindow）─────────────────────────

void UExtraAbilityBase::SetupCancelWindowListener()
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

void UExtraAbilityBase::OnCancelWindowBeginReceived(FGameplayEventData Payload)
{
	EnterCancelWindow();
}

void UExtraAbilityBase::OnCancelWindowEndReceived(FGameplayEventData Payload)
{
	ExitCancelWindow();
}

void UExtraAbilityBase::EnterCancelWindow()
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

void UExtraAbilityBase::ExitCancelWindow()
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

// ── 推力（Push）─────────────────────────────────────────

void UExtraAbilityBase::SetupPushSelfListener()
{
	UAbilityTask_WaitGameplayEvent* WaitPushTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, UUExtraAbilitySystemStatic::GetPushSelfTag(), nullptr, false, false);
	WaitPushTask->EventReceived.AddDynamic(this, &ThisClass::OnPushSelfNotifyReceived);
	WaitPushTask->ReadyForActivation();
}

void UExtraAbilityBase::OnPushSelfNotifyReceived(FGameplayEventData Payload)
{
	FPushTargetData PushData;
	if (!ExtractPushData(Payload.TargetData, PushData))
	{
		return;
	}

	PushSelf(PushData.PushVelocity, PushData.bOverrideXY, PushData.bOverrideZ);
}

bool UExtraAbilityBase::ExtractPushData(const FGameplayAbilityTargetDataHandle& Handle, FPushTargetData& OutData)
{
	for (int32 i = 0; i < Handle.Num(); ++i)
	{
		const FGameplayAbilityTargetData* Data = Handle.Get(i);
		if (Data && Data->GetScriptStruct() == FPushTargetData::StaticStruct())
		{
			OutData = *static_cast<const FPushTargetData*>(Data);
			return true;
		}
	}
	return false;
}

void UExtraAbilityBase::PushSelf(const FVector& PushVel, bool bOverrideXY, bool bOverrideZ)
{
	// 取 Avatar 本身而不是 AExtraPlayerCharacter：被动 GA / AI 角色也能吃到推力
	if (ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		AvatarCharacter->LaunchCharacter(PushVel, bOverrideXY, bOverrideZ);
	}
}

void UExtraAbilityBase::PushTarget(AActor* Target, const FVector& PushVel)
{
	if (!Target) return;

	FGameplayEventData EventData;

	FGameplayAbilityTargetData_SingleTargetHit* HitData = new FGameplayAbilityTargetData_SingleTargetHit;
	FHitResult HitResult;
	HitResult.ImpactNormal = PushVel;

	HitData->HitResult = HitResult;
	EventData.TargetData.Add(HitData);

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Target, UUExtraAbilitySystemStatic::GetLaunchedAbilityActivationTag(), EventData);
}

void UExtraAbilityBase::PushTargets(const TArray<AActor*>& Targets, const FVector PushVel)
{
	for (AActor* Target : Targets)
	{
		PushTarget(Target, PushVel);
	}
}

void UExtraAbilityBase::PushTargets(const FGameplayAbilityTargetDataHandle& TargetDataHandle, const FVector& PushVel)
{
	TArray<AActor*> Targets = UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(TargetDataHandle);
	PushTargets(Targets, PushVel);
}

void UExtraAbilityBase::PushTargetsFromLocation(const FGameplayAbilityTargetDataHandle& TargetDataHandle, const FVector& FromLocation, float PushSpeed)
{
	const TArray<AActor*> Targets = UAbilitySystemBlueprintLibrary::GetAllActorsFromTargetData(TargetDataHandle);

	PushTargetsFromLocation(Targets, FromLocation, PushSpeed);
}

void UExtraAbilityBase::PushTargetsFromOwnerLocation(const TArray<AActor*>& Targets, float PushSpeed)
{
	AActor* OwnerAvatarActor = GetAvatarActorFromActorInfo();
	if (!OwnerAvatarActor) return;

	FVector OwnerAvatarActorLocation = OwnerAvatarActor->GetActorLocation();
	PushTargetsFromLocation(Targets, OwnerAvatarActorLocation, PushSpeed);
}

void UExtraAbilityBase::PushTargetsFromLocation(const TArray<AActor*>& Targets, const FVector& FromLocation, float PushSpeed)
{
	for (AActor* Target : Targets)
	{
		FVector PushDir = Target->GetActorLocation() - FromLocation;
		PushDir.Z = 0;
		PushDir.Normalize();

		PushTarget(Target, PushDir * PushSpeed);
	}
}
