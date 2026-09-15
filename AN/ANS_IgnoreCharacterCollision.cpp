#include "ANS_IgnoreCharacterCollision.h"
#include "Animation/AnimInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
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

	// 蒙太奇被强行打断时 NotifyEnd 不保证到达，挂 BlendOut 兜底恢复碰撞
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

	// 穿透对象：场上所有 ExtraCharacter（不限于锁定目标）。
	// MoveIgnoreActorAdd 内部是 AddUnique，重复添加无副作用。
	for (TActorIterator<AExtraCharacter> It(World); It; ++It)
	{
		AExtraCharacter* Other = *It;
		if (!Other || Other == OwnerChar)
		{
			continue;
		}

		OwnerChar->MoveIgnoreActorAdd(Other);
		IgnoredActors.Add(Other);
	}
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
			}
		}
	}

	IgnoredActors.Reset();
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
