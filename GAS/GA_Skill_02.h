// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Skill_02.generated.h"

/**
 * E 技能：两层充能，表现按角色当前位置分派（升空斩 / 落地斩）。
 *
 * - 地面按：段1 上斩击并升空（单 Montage，播完结束 GA；升空位移由 Montage 根位移提供）
 * - 空中按：段2 落地斩（起手 → 循环下砸 → 落地，衔接逻辑同 GA_AirAttack）
 * - 段1 播放中再按：消耗第二层并立即中断段1 切段2；段2 播放中不再响应
 *   （靠 AbilityTask_WaitGameplayEvent 的 OnlyTriggerOnce 实现，不需要额外状态位）
 *
 * 充能不自己计数：Cooldown GE 的 stack 数即「已消耗、正在回充」的层数，
 * 因此 override CheckCooldown，把原生「cooldown tag 在就封锁」放宽为「层数用满才封锁」。
 * 消耗仍走原生 CommitAbility / CommitAbilityCooldown，由 GAS 自动给 Cooldown GE 叠层。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Skill_02 : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Skill_02();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 把原生「cooldown GE 的 tag 在 ASC 上就封锁」改成「充能层数用满才封锁」
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

private:
	// ── 动画 ──────────────────────────────────────────────
	// 段1：上斩击并升空（升空靠根位移，GA 不加推力）
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* RiseMontage;

	// 段2 落地斩三段（配置与衔接方式同 GA_AirAttack）
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackLoopMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackLandMontage;

	// 段间 blend 时间不在这里配：交接时各段用自己 Montage 资产里的 BlendIn / BlendOut。
	// 注意 Start → Loop 的衔接依赖两者的 BlendOut / BlendIn 在资产里配成有意重叠，否则会出现真空帧。

	// 当前阶段。停某个 Montage 会同步触发它的 Interrupted/Ended 回调，
	// 靠阶段判断区分「正常推进（先改了阶段）」与「外部打断」，避免误结束 GA。
	enum class ESkill02Phase : uint8
	{
		None,
		Rise,
		LandStart,
		LandLoop,
		LandLand
	};
	ESkill02Phase CurrentPhase = ESkill02Phase::None;

	// 本次激活是否已过段1 的「升空到位」标记帧（AN_Skill02RiseReady，每次激活重置）
	bool bRiseReadyMarked = false;

	// 段1 期间是否按过 E（含早于标记帧按下的那一次）：标记帧到达后据此补一次衔接，
	// 否则「升空后快速双击」里落在标记帧前的第二下会白按
	bool bPendingSkillInput = false;

	// ── 段1：升空斩 ────────────────────────────────────────
	void PlayRiseMontage();

	// 延迟到下一帧挂载「第二次技能输入」监听（原因见 PlayRiseMontage 内的注释）
	void SetupWaitSkillInput();

	UFUNCTION()
	void OnRiseMontageFinished();

	UFUNCTION()
	void OnRiseMontageInterrupted();

	// 段1 播到「升空到位」标记帧后置位，并补一次早于标记帧按下的输入
	UFUNCTION()
	void OnRiseReadyMarked(FGameplayEventData Payload);

	// 段1 播放期间收到技能输入：只记「玩家按过 E」，此刻能否切交给 TryEnterLandAttack 判定
	UFUNCTION()
	void OnSkillInputDuringRise(FGameplayEventData Payload);

	// 满足「已过标记帧 + 仍在空中 + 有挂起输入」时消耗第二层充能并切落地斩
	void TryEnterLandAttack();

	// ── 段2：落地斩 ────────────────────────────────────────
	// 进入落地斩：先切阶段再停段1，然后播起手段
	void EnterLandAttack();

	void PlayLandAttackStartMontage();

	UFUNCTION()
	void OnLandAttackStartBlendOut();

	UFUNCTION()
	void OnLandAttackStartInterrupted();

	void PlayLandAttackLoopMontage();

	UFUNCTION()
	void OnLandAttackLoopEnded(UAnimMontage* Montage, bool bInterrupted);

	// 落地检测：LandedDelegate 事件 + Timer 轮询双保险（同 GA_AirAttack）
	UFUNCTION()
	void OnLandDetected(const FHitResult& Hit);

	UFUNCTION()
	void PollLandCheck();

	void TryTriggerLand();

	void PlayLandAttackLandMontage();

	void StopLandAttackLoopMontage();

	UFUNCTION()
	void OnLandAttackLandMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	FTimerHandle LandCheckTimerHandle;
};
