#include "GA_Evade.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"

void UGA_Evade::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!K2_CommitAbility())
	{
		K2_EndAbility();
		return;
	}

	ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarChar)
	{
		K2_EndAbility();
		return;
	}

	// 根据前向速度选 Montage：有前向速度 → 前冲，否则 → 后冲
	const FVector Velocity = AvatarChar->GetVelocity();
	const FVector ForwardDir = AvatarChar->GetActorForwardVector();
	const float FwdSpeed = FVector::DotProduct(Velocity, ForwardDir);

	CurrentPlayingMontage = (FwdSpeed > 0.f) ? ForwardEvadeMontage : BackwardEvadeMontage;
	if (!CurrentPlayingMontage)
	{
		K2_EndAbility();
		return;
	}

	// 直接通过 AnimInstance 播放 Montage，走 UE 原生 Root Motion 管线
	// 不用 GAS 的 PlayMontageAndWait，后者会让 Root Motion 被 GAS 提取后未正确写入 CMC
	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
		if (!AnimInst)
		{
			K2_EndAbility();
			return;
		}

		AnimInst->Montage_Play(CurrentPlayingMontage);

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &UGA_Evade::OnEvadeMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, CurrentPlayingMontage);
	}
}

void UGA_Evade::OnEvadeMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	K2_EndAbility();
}

void UGA_Evade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (CurrentPlayingMontage)
	{
		ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
		if (AvatarChar)
		{
			UAnimInstance* AnimInst = AvatarChar->GetMesh()->GetAnimInstance();
			if (AnimInst && AnimInst->Montage_IsPlaying(CurrentPlayingMontage))
			{
				AnimInst->Montage_Stop(0.1f, CurrentPlayingMontage);
			}
		}
		CurrentPlayingMontage = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
