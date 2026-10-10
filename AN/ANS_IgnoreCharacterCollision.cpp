#include "ANS_IgnoreCharacterCollision.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

// 兜底自检周期：只在有窗口存在时跑，粒度够细且几乎不占开销
static constexpr float PassThroughWatchdogInterval = 0.2f;

void UANS_IgnoreCharacterCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AExtraCharacter* OwnerChar = MeshComp ? Cast<AExtraCharacter>(MeshComp->GetOwner()) : nullptr;
	if (OwnerChar)
	{
		OpenWindow(OwnerChar, MeshComp, Cast<UAnimMontage>(Animation));
	}
}

void UANS_IgnoreCharacterCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (AExtraCharacter* OwnerChar = MeshComp ? Cast<AExtraCharacter>(MeshComp->GetOwner()) : nullptr)
	{
		CloseWindow(OwnerChar);
	}
}

void UANS_IgnoreCharacterCollision::OpenWindow(AExtraCharacter* OwnerChar, USkeletalMeshComponent* MeshComp, UAnimMontage* Source)
{
	// 同一角色重复进入（蒙太奇重播）先收掉自己上一份，避免叠加
	CloseWindow(OwnerChar);
	PruneInvalidEntries();

	FWindowRecord Record;
	Record.Source = Source;
	Record.Mesh = MeshComp;
	Windows.Add(OwnerChar, MoveTemp(Record));

	UWorld* World = OwnerChar->GetWorld();
	FWindowRecord* AddedRecord = Windows.Find(OwnerChar);
	if (World && AddedRecord)
	{
		for (TActorIterator<AExtraCharacter> It(World); It; ++It)
		{
			AExtraCharacter* Other = *It;
			if (!Other || Other == OwnerChar)
			{
				continue;
			}

			// 双向：对手也要忽略发起者。只做单向时，对手自己的移动扫掠仍会撞上发起者
			// 并触发穿透解算，两个竖直胶囊接近同心时最短分离方向落在竖直方向——把对手顶起来。
			Pullers.FindOrAdd(Other).AddUnique(OwnerChar);
			AddedRecord->PulledPeers.Add(Other);

			RebuildStateFor(Other);
		}
	}

	RebuildStateFor(OwnerChar);
	ScheduleWatchdog(OwnerChar);
}

void UANS_IgnoreCharacterCollision::CloseWindow(AExtraCharacter* OwnerChar)
{
	FWindowRecord Record;
	if (!Windows.RemoveAndCopyValue(OwnerChar, Record))
	{
		return;
	}

	if (UWorld* World = OwnerChar->GetWorld())
	{
		World->GetTimerManager().ClearTimer(Record.WatchdogHandle);
	}

	for (const TWeakObjectPtr<AExtraCharacter>& Peer : Record.PulledPeers)
	{
		AExtraCharacter* PeerChar = Peer.Get();
		if (!PeerChar)
		{
			continue;
		}

		if (TArray<TWeakObjectPtr<AExtraCharacter>>* PeerPullers = Pullers.Find(PeerChar))
		{
			PeerPullers->RemoveAll([OwnerChar](const TWeakObjectPtr<AExtraCharacter>& Puller)
			{
				return Puller.Get() == OwnerChar;
			});

			if (PeerPullers->Num() == 0)
			{
				Pullers.Remove(PeerChar);
			}
		}

		RebuildStateFor(PeerChar);
	}

	RebuildStateFor(OwnerChar);
}

void UANS_IgnoreCharacterCollision::RebuildStateFor(AExtraCharacter* Char)
{
	UWorld* World = Char ? Char->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const bool bOwnWindow = Windows.Contains(Char);
	const TArray<TWeakObjectPtr<AExtraCharacter>>* CharPullers = Pullers.Find(Char);
	const bool bPulled = CharPullers && CharPullers->Num() > 0;

	// 期望忽略集：自己有窗口 → 穿过场上所有人；只是被拉入 → 只穿过拉自己进来的那些
	TArray<AActor*> Desired;
	if (bOwnWindow)
	{
		for (TActorIterator<AExtraCharacter> It(World); It; ++It)
		{
			AExtraCharacter* Other = *It;
			if (Other && Other != Char)
			{
				Desired.Add(Other);
			}
		}
	}
	else if (CharPullers)
	{
		for (const TWeakObjectPtr<AExtraCharacter>& Puller : *CharPullers)
		{
			if (AExtraCharacter* Other = Puller.Get())
			{
				Desired.AddUnique(Other);
			}
		}
	}

	// 差量更新自己的移动忽略列表，多退少补
	TArray<TWeakObjectPtr<AActor>>& Applied = AppliedIgnores.FindOrAdd(Char);

	for (int32 Index = Applied.Num() - 1; Index >= 0; --Index)
	{
		AActor* Peer = Applied[Index].Get();
		if (Peer && Desired.Contains(Peer))
		{
			continue;
		}

		Applied.RemoveAtSwap(Index);
		if (Peer)
		{
			Char->MoveIgnoreActorRemove(Peer);
		}
	}

	for (AActor* Other : Desired)
	{
		const bool bAlreadyApplied = Applied.ContainsByPredicate(
			[Other](const TWeakObjectPtr<AActor>& Item) { return Item.Get() == Other; });

		if (!bAlreadyApplied)
		{
			Applied.Add(Other);
			Char->MoveIgnoreActorAdd(Other);
		}
	}

	// 斥力：自己有窗口或被拉入期间关掉，两者都没有时按原值恢复
	UCharacterMovementComponent* Movement = Char->GetCharacterMovement();
	bool* Backup = PhysicsBackups.Find(Char);
	const bool bWantDisabled = bOwnWindow || bPulled;

	if (bWantDisabled && !Backup)
	{
		if (Movement)
		{
			// bEnablePhysicsInteraction 是 uint8 位域，显式转 bool 备份
			PhysicsBackups.Add(Char, Movement->bEnablePhysicsInteraction != 0);
			Movement->bEnablePhysicsInteraction = false;
		}
	}
	else if (!bWantDisabled && Backup)
	{
		if (Movement)
		{
			Movement->bEnablePhysicsInteraction = *Backup;
		}

		PhysicsBackups.Remove(Char);
	}
}

void UANS_IgnoreCharacterCollision::ScheduleWatchdog(AExtraCharacter* OwnerChar)
{
	FWindowRecord* Record = Windows.Find(OwnerChar);
	if (!Record)
	{
		return;
	}

	UWorld* World = OwnerChar->GetWorld();
	if (!World)
	{
		return;
	}

	// 单次 + 自续期：角色或窗口没了就不再续期，定时器自行结束
	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(
		this, &ThisClass::OnWindowWatchdog, TWeakObjectPtr<AExtraCharacter>(OwnerChar));

	World->GetTimerManager().SetTimer(Record->WatchdogHandle, Delegate, PassThroughWatchdogInterval, false);
}

void UANS_IgnoreCharacterCollision::OnWindowWatchdog(TWeakObjectPtr<AExtraCharacter> OwnerChar)
{
	AExtraCharacter* Char = OwnerChar.Get();
	if (!Char)
	{
		// 角色已销毁：不续期，留下的失效键交给惰性剔除
		return;
	}

	const FWindowRecord* Record = Windows.Find(Char);
	if (!Record)
	{
		return;
	}

	const UAnimMontage* Source = Record->Source.Get();
	USkeletalMeshComponent* Mesh = Record->Mesh.Get();
	UAnimInstance* AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;

	// 承载区间的蒙太奇已不在播 → NotifyEnd 丢了，主动收窗。
	// Source 为空（非蒙太奇承载）时不参与判定，只靠 NotifyEnd。
	if (Source && (!AnimInst || !AnimInst->Montage_IsPlaying(Source)))
	{
		CloseWindow(Char);
		return;
	}

	ScheduleWatchdog(Char);
}

void UANS_IgnoreCharacterCollision::PruneInvalidEntries()
{
	// 角色被销毁后没有再进来的机会，留下的失效键只能惰性剔除。
	// 失效窗口的发起者自己不用管（actor 都没了），但它拉过的对手必须注销：
	// 否则对手的斥力会一直挂在关闭状态，它的忽略名单里也留着一条指向已销毁角色的边。
	TArray<TWeakObjectPtr<AExtraCharacter>> PeersToRebuild;
	for (auto It = Windows.CreateIterator(); It; ++It)
	{
		if (It.Key().IsValid())
		{
			continue;
		}

		for (const TWeakObjectPtr<AExtraCharacter>& Peer : It.Value().PulledPeers)
		{
			if (Peer.IsValid())
			{
				PeersToRebuild.AddUnique(Peer);
			}
		}

		It.RemoveCurrent();
	}

	for (const TWeakObjectPtr<AExtraCharacter>& Peer : PeersToRebuild)
	{
		AExtraCharacter* PeerChar = Peer.Get();
		if (!PeerChar)
		{
			continue;
		}

		if (TArray<TWeakObjectPtr<AExtraCharacter>>* PeerPullers = Pullers.Find(PeerChar))
		{
			PeerPullers->RemoveAll([](const TWeakObjectPtr<AExtraCharacter>& Puller) { return !Puller.IsValid(); });

			if (PeerPullers->Num() == 0)
			{
				Pullers.Remove(PeerChar);
			}
		}

		RebuildStateFor(PeerChar);
	}

	for (auto It = AppliedIgnores.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		It.Value().RemoveAll([](const TWeakObjectPtr<AActor>& Item) { return !Item.IsValid(); });
	}

	for (auto It = Pullers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		It.Value().RemoveAll([](const TWeakObjectPtr<AExtraCharacter>& Item) { return !Item.IsValid(); });
	}

	for (auto It = PhysicsBackups.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FString UANS_IgnoreCharacterCollision::GetNotifyName_Implementation() const
{
	return TEXT("Ignore Character Collision");
}
