#include "ULockOnComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

ULockOnComponent::ULockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULockOnComponent::BeginPlay()
{
	Super::BeginPlay();

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	// 检测只在本机执行（本地控制的 Pawn），服务端/模拟端不跑，避免重复锁定
	if (!OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(DetectTimerHandle, this, &ThisClass::UpdateLockTarget, DetectInterval, true);
	}
}

void ULockOnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DetectTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ULockOnComponent::ClearLockTarget()
{
	CurrentLockTarget.Reset();
}

void ULockOnComponent::ForceRefresh()
{
	UpdateLockTarget();
}

void ULockOnComponent::UpdateLockTarget()
{
	if (!GetOwner() || !GetWorld())
	{
		return;
	}

	// 缓存旧目标，用于末尾判断锁定对象是否发生变化
	const TWeakObjectPtr<AActor> PreviousTarget = CurrentLockTarget;

	// 已有目标且保持模式：目标仍有效且在解除距离内 → 保持不动，避免攻击中频繁跳目标
	if (bHoldTargetUntilBreak && CurrentLockTarget.IsValid())
	{
		AActor* Cur = CurrentLockTarget.Get();
		if (IsValidTarget(Cur))
		{
			const float BreakRangeSq = LockBreakRange * LockBreakRange;
			if (FVector::DistSquared(GetOwner()->GetActorLocation(), Cur->GetActorLocation()) <= BreakRangeSq)
			{
				return;
			}
		}
		// 目标失效 / 超距 → 清除，继续扫描
		CurrentLockTarget.Reset();
	}

	const FVector Origin = GetOwner()->GetActorLocation();
	const FCollisionShape Shape = FCollisionShape::MakeSphere(LockRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	TArray<FOverlapResult> Overlaps;
	if (!GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, QueryParams))
	{
		CurrentLockTarget.Reset();
		return;
	}

	// 从候选中挑距离最近的有效目标
	float BestDistSq = TNumericLimits<float>::Max();
	AActor* BestTarget = nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || !IsValidTarget(Candidate))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Origin, Candidate->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestTarget = Candidate;
		}
	}

	CurrentLockTarget = BestTarget;

	// 锁定对象变化时（新锁定 / 解除），Log 与屏幕各打印一次
	if (CurrentLockTarget != PreviousTarget)
	{
		if (AActor* NewTarget = CurrentLockTarget.Get())
		{
			const FString Msg = FString::Printf(TEXT("[LockOn] 锁定目标: %s"), *NewTarget->GetName());
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, Msg);
			}
		}
		else
		{
			const FString Msg = TEXT("[LockOn] 锁定目标: 无");
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, Msg);
			}
		}
	}
}

bool ULockOnComponent::IsValidTarget(AActor* Candidate) const
{
	if (!Candidate || Candidate == GetOwner())
	{
		return false;
	}

	if (!IsEnemy(Candidate) || !IsAlive(Candidate))
	{
		return false;
	}

	if (bRequireLOS && !HasLineOfSight(Candidate))
	{
		return false;
	}

	return true;
}

bool ULockOnComponent::IsEnemy(AActor* Candidate) const
{
	const AExtraCharacter* OwnerChar = Cast<AExtraCharacter>(GetOwner());
	const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(Candidate);
	if (!OwnerChar || !TeamAgent)
	{
		return false;
	}
	return TeamAgent->GetGenericTeamId() != OwnerChar->GetGenericTeamId();
}

bool ULockOnComponent::IsAlive(AActor* Candidate) const
{
	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Candidate);
	if (!ASI)
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}
	return ASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute()) > 0.f;
}

bool ULockOnComponent::HasLineOfSight(AActor* Candidate) const
{
	if (!GetOwner() || !GetWorld())
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.AddIgnoredActor(Candidate);

	return !GetWorld()->LineTraceSingleByChannel(
		Hit,
		GetOwner()->GetActorLocation(),
		Candidate->GetActorLocation(),
		ECC_Visibility,
		QueryParams);
}
