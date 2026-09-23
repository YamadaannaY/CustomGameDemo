#include "ANS_IgnoreCharacterCollision.h"
#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtractGameCharacter/ExtraCharacter.h"

void UANS_IgnoreCharacterCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwnerChar)
	{
		return;
	}

	OwnerCharacter = OwnerChar;
	ApplyIgnore();

	// 蒙太奇被强行打断时挂 BlendOut 恢复碰撞
	if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
	{
		BoundAnimInstance = AnimInst;
		AnimInst->OnMontageBlendingOut.AddDynamic(this, &ThisClass::OnOwnerMontageBlendingOut);
	}
}

void UANS_IgnoreCharacterCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UAnimInstance* AnimInst = BoundAnimInstance.Get())
	{
		AnimInst->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::OnOwnerMontageBlendingOut);
	}
	BoundAnimInstance = nullptr;

	ClearIgnore();
}

void UANS_IgnoreCharacterCollision::ApplyIgnore()
{
	ACharacter* OwnerChar = OwnerCharacter.Get();
	UWorld* World = OwnerChar ? OwnerChar->GetWorld() : nullptr;
	if (!OwnerChar || !World)
	{
		return;
	}

	// 先清掉上一次的残留，避免重复进入区间时叠加
	ClearIgnore();

	// 穿透对象：场上所有 ExtraCharacter
	// MoveIgnoreActorAdd 内部是 AddUnique，重复添加无副作用。
	for (TActorIterator<AExtraCharacter> It(World); It; ++It)
	{
		AExtraCharacter* Other = *It;
		if (!Other || Other == OwnerChar)
		{
			continue;
		}

		// 双向忽略
		OwnerChar->MoveIgnoreActorAdd(Other);
		Other->MoveIgnoreActorAdd(OwnerChar);
		IgnoredActors.Add(Other);

		// 位移半径小于两胶囊半径之和时重叠是必然状态，关掉斥力避免每帧被顶开
		DisablePhysicsInteraction(OwnerChar);
		DisablePhysicsInteraction(Other);
	}
}

void UANS_IgnoreCharacterCollision::DisablePhysicsInteraction(ACharacter* Character)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	// 重复进入区间时保持首次记录的原值，避免把已关闭的状态当成原值
	for (const TPair<TWeakObjectPtr<UCharacterMovementComponent>, bool>& Backup : PhysicsInteractionBackups)
	{
		if (Backup.Key.Get() == Movement)
		{
			return;
		}
	}

	// bEnablePhysicsInteraction 是 uint8 位域，显式转 bool 以匹配备份表类型
	PhysicsInteractionBackups.Emplace(Movement, Movement->bEnablePhysicsInteraction != 0);
	Movement->bEnablePhysicsInteraction = false;
}

void UANS_IgnoreCharacterCollision::ClearIgnore()
{
	if (ACharacter* OwnerChar = OwnerCharacter.Get())
	{
		for (const TWeakObjectPtr<AActor>& Ignored : IgnoredActors)
		{
			if (AActor* IgnoredActor = Ignored.Get())
			{
				OwnerChar->MoveIgnoreActorRemove(IgnoredActor);

				if (ACharacter* IgnoredChar = Cast<ACharacter>(IgnoredActor))
				{
					IgnoredChar->MoveIgnoreActorRemove(OwnerChar);
				}
			}
		}
	}

	IgnoredActors.Reset();

	for (const TPair<TWeakObjectPtr<UCharacterMovementComponent>, bool>& Backup : PhysicsInteractionBackups)
	{
		if (UCharacterMovementComponent* Movement = Backup.Key.Get())
		{
			Movement->bEnablePhysicsInteraction = Backup.Value;
		}
	}

	PhysicsInteractionBackups.Reset();
}

void UANS_IgnoreCharacterCollision::OnOwnerMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// NotifyEnd 已处理过时这里是空操作（列表已清空）
	ClearIgnore();
}

FString UANS_IgnoreCharacterCollision::GetNotifyName_Implementation() const
{
	return TEXT("Ignore Character Collision");
}
