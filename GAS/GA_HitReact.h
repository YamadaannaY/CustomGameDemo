// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ExtraPassiveGameplayAbility.h"
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

	// 窗口长度（秒）
	UPROPERTY(EditDefaultsOnly, Category = "HitReact|KnockDown", meta = (ClampMin = "0.0"))
	float KnockDownWindow = 5.f;

	// 窗口内累计受击达到这个次数时，本次改为触发倒地
	UPROPERTY(EditDefaultsOnly, Category = "HitReact|KnockDown", meta = (ClampMin = "2"))
	int32 HitCountToKnockDown = 3;
	
	//窗口内达到指定的受击次数，变为倒地
	bool ShouldKnockDown(float Now);

	// 窗口内的受击时间戳
	TArray<float> RecentHitTimes;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayHitReactTask;
};
