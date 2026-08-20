#include "AN_AirAttackDive.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UAN_AirAttackDive::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character || !Character->GetCharacterMovement())
	{
		return;
	}

	// 下砸方向：角色前方（水平）+ 向下，构成俯冲角
	// DiveAngle 越大越偏竖直向下，越小越偏水平前方
	const FVector Forward = Character->GetActorForwardVector();
	const FVector Down = FVector::UpVector * -1.0f;

	// 用俯冲角混合「前方」与「竖直向下」，得到俯冲方向
	const float AngleRad = FMath::DegreesToRadians(DiveAngle);
	const FVector DiveDir = (Forward * FMath::Cos(AngleRad) + Down * FMath::Sin(AngleRad)).GetSafeNormal();

	// 关键：LaunchCharacter 的 bXYOverride / bZOverride 都传 true，覆盖（而非叠加）当前速度，
	// 清掉起跳/跳跃残余的向上速度，避免「先上飘再下砸」。
	Character->LaunchCharacter(DiveDir * DiveSpeed, true, true);

	// 落点偏移仅用于参考，暂不强制 teleport，实际落点由速度 + 重力决定
}

FString UAN_AirAttackDive::GetNotifyName_Implementation() const
{
	return TEXT("AirAttackDive");
}
