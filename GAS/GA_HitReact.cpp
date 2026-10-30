// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_HitReact.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_HitReact::UGA_HitReact()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetHitReactEventTag());
	SetAssetTags(AssetTags);
	
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = UUExtraAbilitySystemStatic::GetHitReactEventTag();
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
	
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAbilityTag());
	
	bRetriggerInstancedAbility = true;
}

void UGA_HitReact::OnReactActivated(const FGameplayEventData* TriggerEventData)
{
	if (PlayHitReactTask)
	{
		PlayHitReactTask->OnCompleted.RemoveDynamic(this, &ThisClass::OnHitReactMontageFinished);
		PlayHitReactTask->OnBlendOut.RemoveDynamic(this, &ThisClass::OnHitReactMontageFinished);
		PlayHitReactTask->OnInterrupted.RemoveDynamic(this, &ThisClass::OnHitReactMontageFinished);
		PlayHitReactTask->OnCancelled.RemoveDynamic(this, &ThisClass::OnHitReactMontageFinished);
		PlayHitReactTask->EndTask();
		PlayHitReactTask = nullptr;
	}
	const AActor* Attacker = TriggerEventData ? TriggerEventData->Instigator : nullptr;
	UAnimMontage* Montage = SelectHitReactMontage(Attacker);
	
	if (!Montage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HitReact] 未配置受击 Montage，检查 BP_HitReact 的 HitReact|Montage"));
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		UE_LOG(LogTemp, Warning, TEXT("[HitReact] CommitAbility 失败（冷却未过 / 缺 Cost）"));
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (AvatarCharacter && AvatarCharacter->CustomTimeDilation <= KINDA_SMALL_NUMBER)
	{
		PendingMontage = Montage;
		StartWaitingForUnfreeze();
		return;
	}

	PlayHitReactMontage(Montage);
}

void UGA_HitReact::PlayHitReactMontage(UAnimMontage* Montage)
{
	PlayHitReactTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage);
	PlayHitReactTask->OnCompleted.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnBlendOut.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnInterrupted.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnCancelled.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->ReadyForActivation();
}

void UGA_HitReact::StartWaitingForUnfreeze()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		K2_EndAbility();
		return;
	}

	World->GetTimerManager().SetTimer(UnfreezeCheckTimerHandle, this, &UGA_HitReact::TryPlayAfterUnfreeze, 0.05f, true);}

void UGA_HitReact::TryPlayAfterUnfreeze()
{
	const ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!AvatarCharacter || AvatarCharacter->CustomTimeDilation > KINDA_SMALL_NUMBER)
	{
		UAnimMontage* Montage = PendingMontage;
		PendingMontage = nullptr;
		StopWaitingForUnfreeze();

		if (Montage)
		{
			PlayHitReactMontage(Montage);
		}
		else
		{
			K2_EndAbility();
		}
	}
}

void UGA_HitReact::StopWaitingForUnfreeze()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UnfreezeCheckTimerHandle);
	}
}

void UGA_HitReact::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	StopWaitingForUnfreeze();
	PendingMontage = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAnimMontage* UGA_HitReact::SelectHitReactMontage(const AActor* Attacker) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	
	if (!Avatar || !Attacker || Attacker == Avatar)
	{
		return FMath::RandBool() ? LeftHitReactMontage : RightHitReactMontage;
	}

	FVector ToAttacker = Attacker->GetActorLocation() - Avatar->GetActorLocation();
	ToAttacker.Z = 0.f;

	//使用Local空间，将这个向量在局部空间中做投影来判断方向。
	const FVector LocalToAttacker = Avatar->GetActorTransform().InverseTransformVectorNoScale(ToAttacker);

	return LocalToAttacker.Y < 0.f ? LeftHitReactMontage : RightHitReactMontage;
}

void UGA_HitReact::OnHitReactMontageFinished()
{
	K2_EndAbility();
}
