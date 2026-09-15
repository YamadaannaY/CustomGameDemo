#include "ANS_GravityScale.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

float UANS_GravityScale::EvaluateGravity(const USkeletalMeshComponent* MeshComp) const
{
	// 动画上配置了重力曲线就逐帧取曲线值：此时窗口内的重力是一条变化曲线
	if (!GravityCurveName.IsNone())
	{
		if (UAnimInstance* AnimInst = MeshComp ? MeshComp->GetAnimInstance() : nullptr)
		{
			return AnimInst->GetCurveValue(GravityCurveName);
		}
	}

	//回退到一个硬编码值
	return GravityScale;
}

void UANS_GravityScale::ApplyGravity(USkeletalMeshComponent* MeshComp) const
{
	ACharacter* Character = MeshComp ? Cast<ACharacter>(MeshComp->GetOwner()) : nullptr;
	UCharacterMovementComponent* MoveComp = Character ? Character->GetCharacterMovement() : nullptr;
	if (!MoveComp)
	{
		return;
	}

	const float TargetGravity = EvaluateGravity(MeshComp);

	MoveComp->GravityScale = TargetGravity;
}

void UANS_GravityScale::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	ACharacter* Character = MeshComp ? Cast<ACharacter>(MeshComp->GetOwner()) : nullptr;
	if (UCharacterMovementComponent* MoveComp = Character ? Character->GetCharacterMovement() : nullptr)
	{
		CachedGravityScale.Add(MeshComp, MoveComp->GravityScale);

		// 重力归零把 Z 速度一并清掉，从下坠直接切到悬停
		if (bResetVerticalVelocityOnBegin)
		{
			MoveComp->Velocity.Z = 0.f;
		}
	}

	ApplyGravity(MeshComp);
}

void UANS_GravityScale::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	ApplyGravity(MeshComp);
}

void UANS_GravityScale::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	const float* CachedGravity = CachedGravityScale.Find(MeshComp);
	if (!CachedGravity)
	{
		return;
	}

	if (bRestoreOnEnd)
	{
		if (ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				// 恢复进窗前的值
				MoveComp->GravityScale = *CachedGravity;
			}
		}
	}

	CachedGravityScale.Remove(MeshComp);
}

FString UANS_GravityScale::GetNotifyName_Implementation() const
{
	if (!GravityCurveName.IsNone())
	{
		return FString::Printf(TEXT("GravityScale (%s)"), *GravityCurveName.ToString());
	}

	return FString::Printf(TEXT("GravityScale (%.2f)"), GravityScale);
}
