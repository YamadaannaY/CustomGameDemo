#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Skill_02.generated.h"

/**
 *  二阶段 E 技能：两层充能，表现按角色当前位置分派（升空斩 / 落地斩）。
 *
 * - 地面按：段1 上斩击并升空（单 Montage，播完结束 GA）
 * - 空中按：段2 落地斩（起手 → 循环下砸 → 检测落地，衔接逻辑复刻 GA_AirAttack）
 * - 段1 播放中再按：消耗充能并立即中断段1切入段2
 *  充能不自己计数：Cooldown GE 的 stack 数即「已消耗、正在回充」的层数，在CheckCooldown中判定，CommitGA直接使用结果
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Skill_02 : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Skill_02();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 把原生「cooldown GE 的 tag 在 ASC 上就封锁」逻辑重构为「充能层数用满才封锁」
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void DoDamage(const FGameplayEventData& Data) override;
private:
	UPROPERTY(EditDefaultsOnly,Category= "Energy")
	float EnergyValuePerHit = 10.f;
	
	// 段1：上斩击
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* RiseMontage;

	// 段2 落地斩起手段
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackStartMontage;

	// 段2 落地斩循环段
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackLoopMontage;
	
	// 段2 落地斩落地段
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandAttackLandMontage;
	
	// 当前阶段。停某个 Montage 会同步触发它的 Interrupted/Ended 回调，
	// 靠阶段判断区分「正常推进」与「外部打断」，避免误结束 GA。
	enum class ESkill02Phase : uint8
	{
		None,
		Rise,
		LandStart,
		LandLoop,
		LandLand
	};
	ESkill02Phase CurrentPhase = ESkill02Phase::None;

	// 本次激活是否已过段1 的 AN 标记帧
	bool bRiseReadyNotify = false;

	// 段1 期间是否按过 E
	bool bPendingSkillInput = false;

	// ── 长按技能进居合 ────────────────────────────────────
	// 长按 E 到 Montage 的居合检测帧（AN_Skill02JuheCheck）且能量足够时，
	// 挂 JuheReady 并发一次闪避输入，交接给 GA_Evade_Juhe 进居合架势；
	// 空中/地面由 GA_Evade_Juhe 自己按 IsFalling() 决定，这里不分派。
	// 门槛须与 GA_Evade_Juhe::JuheEnergyThreshold 保持一致。
	UPROPERTY(EditDefaultsOnly, Category = "Juhe")
	float JuheEnergyThreshold = 100.f;

	// 挂「居合检测帧」监听（段1 与落地斩的 Land 段各挂一次，各只触发一次）
	void SetupWaitJuheCheck();

	// 检测帧回调：判定「E 仍按住 + 能量足够」后交接给居合 GA
	UFUNCTION()
	void OnJuheCheckFrame(FGameplayEventData Payload);

	// 段1播放
	void PlayRiseMontage();

	// 播放RiseMontage的下一帧挂载「第二次技能输入」监听，如果是Land起手，没有第二段的机会
	void SetupWaitSkillInput();
	
	UFUNCTION()
	void OnRiseMontageFinished();

	UFUNCTION()
	void OnRiseMontageInterrupted();

	// 段1播到AN标记帧后调用，并视缓存变量补一次早于标记帧按下的输入
	UFUNCTION()
	void OnRiseNotifyMarked(FGameplayEventData Payload);

	// 段1播放期间收到技能输入：只记有触发输入，此刻能否切交给 TryEnterLandAttack 判定
	UFUNCTION()
	void OnSkillInputDuringRise(FGameplayEventData Payload);

	// 能否进入段2的判定，满足「已过标记帧 + 仍在空中 + 有挂起输入」时消耗第二层充能并切落地斩
	void TryEnterLandAttack();

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
