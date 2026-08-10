#include "GA_Evade.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/ExtraGameAnimInstance.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
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

	const FVector Velocity = AvatarChar->GetVelocity();
	const FVector ForwardDir = AvatarChar->GetActorForwardVector();
	const float FwdSpeed = FVector::DotProduct(Velocity, ForwardDir);

	CurrentPlayingMontage = (FwdSpeed > 0.f) ? ForwardEvadeMontage : BackwardEvadeMontage;
	if (!CurrentPlayingMontage)
	{
		K2_EndAbility();
		return;
	}

	bTransitionedToSprint = false;

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
		
		
		UAbilityTask_WaitGameplayEvent* WaitToSprintTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,FGameplayTag::RequestGameplayTag("Evade.ToSprint"));
		WaitToSprintTask->EventReceived.AddDynamic(this,&ThisClass::OnEvadeToSprint);
		
		WaitToSprintTask->ReadyForActivation();
	}
}

void UGA_Evade::OnEvadeToSprint(FGameplayEventData EventData)
{
	UE_LOG(LogTemp,Warning,TEXT("123"));
	if (bTransitionedToSprint)
	{
		return;
	}
	bTransitionedToSprint = true;

	AExtraPlayerCharacter* PlayerChar = Cast<AExtraPlayerCharacter>(GetAvatarActorFromActorInfo());
	if (!PlayerChar)
	{
		return;
	}

	UAnimInstance* AnimInst = PlayerChar->GetMesh()->GetAnimInstance();
	if (!AnimInst || !CurrentPlayingMontage)
	{
		return;
	}

	// 截断 Montage（BlendOut=0，立即停止 Root Motion 写入速度）
	AnimInst->Montage_Stop(SprintTransitionBlendOut, CurrentPlayingMontage);

	// 注入 Sprint 速度到 CMC
	FVector Vel = PlayerChar->GetActorForwardVector() * 800.f;
	Vel.Z = 0.f;
	PlayerChar->SprintTransitionVelocity = Vel;

	// 设过渡标志
	UExtraGameAnimInstance* GameAI = Cast<UExtraGameAnimInstance>(AnimInst);
	if (GameAI)
	{
		GameAI->bEvadeToSprint = true;
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
				AnimInst->Montage_Stop(0.2f, CurrentPlayingMontage);
			}
		}
		CurrentPlayingMontage = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
