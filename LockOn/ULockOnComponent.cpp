#include "ULockOnComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

ULockOnComponent::ULockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
}

void ULockOnComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ULockOnComponent, CurrentLockTarget);
}

void ULockOnComponent::BeginPlay()
{
	Super::BeginPlay();

	// 检测只在服务端跑：客户端不能自己决定锁定目标，且锁定目标会决定攻击朝向/位移，必须权威
	if (!GetOwner() || !GetOwner()->HasAuthority())
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
	// 锁定状态只有服务端权威
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!CurrentLockTarget)
	{
		return;
	}

	CurrentLockTarget = nullptr;
	
	LogLockTargetChanged();
}

void ULockOnComponent::ForceRefresh()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UpdateLockTarget();
}

void ULockOnComponent::OnRep_CurrentLockTarget()
{
	LogLockTargetChanged();
}

void ULockOnComponent::LogLockTargetChanged() const
{
	if (GetOwnerRole() == ROLE_SimulatedProxy || GetOwnerRole()==ROLE_Authority) return ;
	
	AActor* MyOwner = GetOwner();
	if (!MyOwner)
	{
		return ; 	
	}
	
	FString OwnerName = MyOwner->GetName();
	
	if (AActor* NewTarget = CurrentLockTarget.Get())
	{
		const FString Msg = FString::Printf(TEXT("[LockOn] %s 锁定目标: %s"), *OwnerName,*NewTarget->GetName());
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, Msg);
		}
	}
	else
	{
		const FString Msg = TEXT("[LockOn] %s 锁定目标: 无",*OwnerName);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, Msg);
		}
	}
}

void ULockOnComponent::UpdateLockTarget()
{
	if (!GetOwner() || !GetWorld())
	{
		return;
	}

	// 缓存旧目标，用于末尾判断锁定对象是否发生变化
	AActor* const PreviousTarget = CurrentLockTarget.Get();

	// 已有目标且保持模式：目标仍有效且在解除距离内 → 保持不动，避免攻击中频繁跳目标
	bool bRescan = true;
	if (bHoldTargetUntilBreak && CurrentLockTarget)
	{
		AActor* Cur = CurrentLockTarget;
		if (IsValidTarget(Cur) &&
			FVector::DistSquared(GetOwner()->GetActorLocation(), Cur->GetActorLocation()) <= LockBreakRange * LockBreakRange)
		{
			bRescan = false;
		}
		else
		{
			// 目标失效 / 超距 → 清除,重新扫描一次
			CurrentLockTarget = nullptr;
		}
	}

	if (bRescan)
	{
		const FVector Origin = GetOwner()->GetActorLocation();
		const FCollisionShape Shape = FCollisionShape::MakeSphere(LockRadius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(GetOwner());

		TArray<FOverlapResult> Overlaps;
		if (!GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Shape, QueryParams))
		{
			CurrentLockTarget = nullptr;
		}
		else
		{
			float BestDistSq = TNumericLimits<float>::Max();
			AActor* BestTarget = nullptr;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* Candidate = Overlap.GetActor();
				if (!Candidate || !IsValidTarget(Candidate))
				{
					continue;
				}

				//遍历找到最近的目标
				const float DistSq = FVector::DistSquared(Origin, Candidate->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestTarget = Candidate;
				}
			}

			CurrentLockTarget = BestTarget;
		}
	}

	// 锁定对象变化时（新锁定 / 解除）：服务端自身赋值不触发 OnRep，这里显式打印一次
	if (CurrentLockTarget.Get() == PreviousTarget)
	{
		return;
	}

	LogLockTargetChanged();
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
