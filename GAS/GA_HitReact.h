// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ExtraPassiveGameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "GA_HitReact.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

/**
 * 受击。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_HitReact : public UExtraPassiveGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_HitReact();

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	virtual void OnReactActivated(const FGameplayEventData* TriggerEventData) override;

	// 左侧被击中（攻击者位于被击者左侧）时播放
	UPROPERTY(EditDefaultsOnly, Category = "HitReact|Montage")
	TObjectPtr<UAnimMontage> LeftHitReactMontage;

	// 右侧被击中时播放
	UPROPERTY(EditDefaultsOnly, Category = "HitReact|Montage")
	TObjectPtr<UAnimMontage> RightHitReactMontage;

private:
	UFUNCTION()
	void OnHitReactMontageFinished();

	// 依据攻击者方位选动画：攻击者在被击者本地 Y<0（左侧）→ 左受击；
	// 无有效攻击者时随机，避免总是同一侧
	UAnimMontage* SelectHitReactMontage(const AActor* Attacker) const;

	void PlayHitReactMontage(UAnimMontage* Montage);

	// 时停（CustomTimeDilation=0）期间 Montage 时间不推进，开始播放只会卡在首帧，
	// 且后续命中会反复重触发。改为等解冻后再播一次。
	void StartWaitingForUnfreeze();

	UFUNCTION()
	void TryPlayAfterUnfreeze();

	void StopWaitingForUnfreeze();

	UPROPERTY()
	TObjectPtr<UAnimMontage> PendingMontage;

	FTimerHandle UnfreezeCheckTimerHandle;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayHitReactTask;
};
