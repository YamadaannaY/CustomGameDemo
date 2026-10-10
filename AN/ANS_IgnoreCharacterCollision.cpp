#include "ANS_IgnoreCharacterCollision.h"
#include "Animation/AnimMontage.h"
#include "ExtractGameCharacter/ExtraCharacter.h"

void UANS_IgnoreCharacterCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// 状态挂在角色上：这里只转达开窗，蒙太奇本体用于之后判断窗口是否还有效
	if (AExtraCharacter* OwnerChar = Cast<AExtraCharacter>(MeshComp->GetOwner()))
	{
		OwnerChar->BeginPassThroughWindow(MeshComp, Cast<UAnimMontage>(Animation));
	}
}

void UANS_IgnoreCharacterCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (AExtraCharacter* OwnerChar = Cast<AExtraCharacter>(MeshComp->GetOwner()))
	{
		OwnerChar->EndPassThroughWindow(Cast<UAnimMontage>(Animation));
	}
}

FString UANS_IgnoreCharacterCollision::GetNotifyName_Implementation() const
{
	return TEXT("Ignore Character Collision");
}
