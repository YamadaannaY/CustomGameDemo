#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "ExtraGameplayAbility.h"
#include "GA_HeavyAttackCharge.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UGameplayEffect;

/**
 * 二阶段重击 GA（蓄力斩）：
 * 长按达阈值激活后播「起手」Montage（后撤步 + 蓄力段）；播完仍未松手则切「循环」Montage 。
 * 激活期间一旦收到松手事件（InputTag.HeavyAttackRelease，由AExtraPlayerCharacter的IA松手时广播），
 * 立即停掉当前段并播「结束」Montage，打出前冲斩击。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_HeavyAttackCharge : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_HeavyAttackCharge();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	// 覆写：蓄力段/循环段在播时均可被移动打断，所以End
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return CurrentPlayingMontage; }

private:
	// ── 三段动画（段间衔接由各 Montage 自身 Blend 配置控制）──
	UPROPERTY(EditDefaultsOnly, Category = "Montage")
	UAnimMontage* ChargeStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Montage")
	UAnimMontage* ChargeLoopMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Montage")
	UAnimMontage* ChargeEndMontage;
	
	// 缓存当前正在播放的Montage段
	UPROPERTY()
	UAnimMontage* CurrentPlayingMontage = nullptr;

	// 是否已进入结束段
	bool bInEnding = false;

	// 是否已开始播循环段
	bool bLoopStarted = false;

	// ── 蓄力持续消耗耐力 ──────────────────────────────────────
	
	// 循环段（蓄力维持）期间应用的持续消耗 GE：应为 Infinite + Period，每 Period 扣一次 Stamina。
	// 进入结束段（打出攻击）时移除。
	UPROPERTY(EditDefaultsOnly, Category = "Cost")
	TSubclassOf<UGameplayEffect> ChargeStaminaDrainEffect;

	// 当前已应用的持续消耗 GE 句柄（移除用；未应用时无效）
	FActiveGameplayEffectHandle ChargeStaminaDrainHandle;

	// 应用持续消耗 GE（进循环段时调用，授权端）
	void ApplyStaminaDrain();

	// 移除持续消耗 GE（打出攻击 / 结束 / 被打断时调用，幂等）
	void RemoveStaminaDrain();
	
	// ── 蓄力持续消耗耐力 ──────────────────────────────────────
	

	// 创建并返回指定段的 PlayMontageAndWait 任务（仅创建，不激活；调用方绑定回调后再 ReadyForActivation）
	UAbilityTask_PlayMontageAndWait* PlayMontage(UAnimMontage* Montage);

	// 停掉当前在播的段（Montage_Stop 带 BlendOut）
	void StopCurrentPlayingMontage();

	// 切到结束段：置位 bInEnding → 停当前段 → 播结束 Montage
	void EnterEndPhase();

	// 松手事件回调：进入结束段打出攻击
	UFUNCTION()
	void HandleRelease(FGameplayEventData EventData);

	// 起手段播完：未松手则进循环段
	UFUNCTION()
	void OnStartMontageCompleted();

	// 起手段被打断：主动切段时忽略，外部打断则结束 GA
	UFUNCTION()
	void OnStartMontageInterrupted();

	// 结束段播完 / 被打断 → 结束 GA
	UFUNCTION()
	void OnEndMontageFinished();
};
