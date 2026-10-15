// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_HitReact.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
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

	if (ShouldKnockDown(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f))
	{
		AActor* Avatar = GetAvatarActorFromActorInfo();

		// 攻击者随 TriggerEventData 透传过去，供 KnockDown 决定击飞方向
		const FGameplayEventData KnockData = TriggerEventData ? *TriggerEventData : FGameplayEventData();

		// 先收掉自己再转交：KnockDown 激活时会取消一切进行中的能力
		K2_EndAbility();

		if (Avatar)
		{
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
				Avatar, UUExtraAbilitySystemStatic::GetKnockDownEventTag(), KnockData);
		}
		return;
	}

	PlayHitReactTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage);
	PlayHitReactTask->OnCompleted.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnBlendOut.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnInterrupted.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->OnCancelled.AddDynamic(this, &ThisClass::OnHitReactMontageFinished);
	PlayHitReactTask->ReadyForActivation();
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

bool UGA_HitReact::ShouldKnockDown(float Now)
{
	// 严格滑动窗口：先剔除窗口外的旧记录
	RecentHitTimes.RemoveAll([this, Now](float Time)
	{
		return Now - Time > KnockDownWindow;
	});

	// 窗口内已经攒够（阈值 - 1）次，本次就是压垮的那一下
	if (RecentHitTimes.Num() >= HitCountToKnockDown - 1)
	{
		// 倒地后清空，起身再挨打从头累计
		RecentHitTimes.Reset();
		return true;
	}

	RecentHitTimes.Add(Now);
	return false;
}
