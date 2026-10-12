#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "GA_Evade.generated.h"

class AExtraPlayerCharacter;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;

/**
 * 冲刺技能，可以派生闪避（被攻击时触发）。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Evade : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Evade();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 激活前拦截：空中且本次浮空的空中闪避预算耗尽时，拒绝激活（不进入激活流程、不消耗耐力）。
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	UFUNCTION()
	void OnEvadeToSprint(FGameplayEventData EventData);

	// 二次闪避：第0帧~EvadeToSprint 通知之间的重复 Dodge 输入，重播当前闪避Montage
	UFUNCTION()
	void HandleDodgeInputPress(float TimeWaited);

	// 空中 Evade 落地回调：落地即转入落地段（见 EnterLandPhase），不再直接结束 GA
	UFUNCTION()
	void OnAirEvadeLandDetected(const FHitResult& Hit);

protected:
	// 供居合特化复用：改写 CurrentPlayingMontage 后调用，播/切 Montage 并在播完时结束 GA
	//（内部会先 EndTask 旧任务，避免其 OnInterrupted 误杀 GA）
	void PlayEvadeMontage();

	// 无输入时的原地后闪 Montage（地面 / 空中），居合子类中需要复用，所以提升到protected
	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* BackwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage|Air")
	UAnimMontage* BackwardAirEvadeMontage;

	// ── 空中 Evade 的落地段（基类与居合共用同一段落地动画）──────────────
	// 空中闪避：动画播完但人还在空中时先挂起（TryHoldForAirLanding），落地后换播落地动画。
	// 落地段仍属于本 GA（同 GA_AirAttack_Phase2 的落地处理）：播完即结束 GA；
	// 中途可被打断靠落地 Montage 里的 AN_CancelWindow（进窗 = 视为已取消，任何 GA 提交即取消本 GA）。
	// 结束走 GAS 的能力结束通道，蒙太奇停止照常复制给模拟端（跳跃取消 GA 也走这条）。
	UPROPERTY(EditDefaultsOnly, Category="Montage|Air")
	UAnimMontage* AirLandMontage;

	// 挂落地检测（LandedDelegate + 兜底轮询）。仅空中激活时生效。
	void SetupAirLandDetection();

	// 清掉落地检测（委托 + 轮询 Timer）
	void ClearAirLandDetection();

	// 落地检测兜底轮询：激活瞬间已贴地等边界情况委托不会触发
	UFUNCTION()
	void PollAirLandCheck();

	// 进入落地段：停掉闪避期间的状态（朝向微调 / 输入轮询 / 二次闪避），换播落地动画
	void EnterLandPhase();

	// 停掉当前正在播的闪避段（摘掉任务回调 + 经 ASC 通道停），落地段起播前调用
	void StopCurrentEvadeMontage();

	// 收尾前的等待：仍在空中则不结束 GA（返回 true，落地后再接落地段）；
	// 已贴地则转入落地段（落地段由本 GA 持有，同样返回 true 表示这里先不结束）。
	// 非空中激活或已在落地段返回 false，由调用方正常结束。
	bool TryHoldForAirLanding();

	// 进入落地段前的子类钩子（居合据此停自家 Montage、放弃前冲接续、放行普攻 GA）
	virtual void OnEnterLandPhase() {}

	// 当前正在播放的 Montage
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentPlayingMontage;

	// 结束时是否施加「连续闪避冷却」GE。居合分支不走基类激活流程、DodgeCount 会残留上次的值，
	// 故覆写为 false，避免居合结束误触发闪避冷却。
	virtual bool ShouldApplyDodgeCooldown() const { return true; }

	// 本次激活为空中 Evade / 空中居合（居合特化据此选空中那套动画）
	bool bAirborneEvade = false;

	// 已进入落地段（落地只处理一次；落地动画播完才结束 GA）
	bool bInLanding = false;

private:
	// 挂载 Dodge 输入监听（GameplayEvent 方式，类似 GA_Combo 循环监听 LightAttack）
	void SetupWaitDodgeInputPress();

	// Montage 播完的收尾：空中 Evade 若还在空中则挂起等落地，其余情况结束 GA
	UFUNCTION()
	void OnEvadeMontageFinished();

	// 播放/重播当前 Montage，并接管旧 Montage 被替换时触发的任务回调
	void HandlePlayMontageTaskDelegates(UAbilityTask_PlayMontageAndWait* Task);

	// 当前激活对应的 PlayMontage 任务（重播时先 EndTask 旧任务，避免其 OnInterrupted 误杀 GA）
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayEvadeMontageTask;

	void PollMoveInputForSprint();

	// 依据当前移动输入重新选择本次闪避 montage（前冲需带输入，无输入为原地后闪）并写入朝向/MW target。
	// 首次激活与二次闪避共用：二次闪避不再锁定触发时的 montage，而是重新按当前输入判定与转向。
	// 返回 false 表示无可用 montage（不改动当前状态）。
	bool ReselectEvade(bool bAirborne);

	void UpdateEvadeFacing();
	
	// 将当前 EvadeBaseYaw + CurrentEvadeFacingOffset 写入 MotionWarp target。
	void ApplyEvadeFacingWarp(const AExtraPlayerCharacter* PlayerChar);
	
	UPROPERTY(EditDefaultsOnly, Category="Montage")
	float InputPollInterval = 0.05f;
	
	//左右输入调整朝向可以达到的最大角度
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeMaxRotationAngle = 90.f;

	//旋转每帧插值速度
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeRotationInterpSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* ForwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage|Air")
	UAnimMontage* ForwardAirEvadeMontage;

	// 本次激活选择的是前冲（Forward/ForwardAir），用于朝向调整等前冲专属逻辑
	bool bPlayingForwardEvade = false;

	// 落地检测的兜底轮询间隔
	UPROPERTY(EditDefaultsOnly, Category="Evade|Air")
	float LandCheckInterval = 0.08f;

	FTimerHandle AirLandCheckTimer;

	// 允许的最大连续闪避次数（第0帧算一次，之后到 EvadeToSprint 前可再补一次）
	UPROPERTY(EditDefaultsOnly, Category="Evade")
	int32 MaxDodgeCount = 2;

	FTimerHandle InputPollTimer;
	FTimerHandle EvadeFacingTimer;
	float EvadeBaseYaw = 0.f;
	float CurrentEvadeFacingOffset = 0.f;
	bool bTransitionedToSprint = false;
	bool bIsPollingForInput = false;

	// 本次激活已发生的闪避次数（每重播一次 +1，达到 MaxDodgeCount 后不再响应）
	int32 DodgeCount = 0;
	// EvadeToSprint 通知已触发：此动画帧开始不再响应再次闪避
	bool bEvadeToSprintTriggered = false;
	
	UPROPERTY(EditDefaultsOnly, Category="Cooldown")
	TSubclassOf<UGameplayEffect> MaxDodgeTriggerCooldownEffect;
};
