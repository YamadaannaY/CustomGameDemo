#pragma once

#include "CoreMinimal.h"
#include "GA_Evade.h"
#include "GA_Evade_Juhe.generated.h"

class UAbilityTask_PlayMontageAndWait;

/**
 * 居合用到的三段动画，地面 / 空中各配一套。
 */
USTRUCT()
struct FJuheMontageSet
{
	GENERATED_BODY()

	// 居合架势（后撤 + 架势 + 后摇；内含发分界事件的 AN）
	UPROPERTY(EditDefaultsOnly)
	UAnimMontage* JuheMontage = nullptr;

	// 居合前冲 1（每段居合的首段固定用它）
	UPROPERTY(EditDefaultsOnly)
	UAnimMontage* ForwardMontage1 = nullptr;

	// 居合前冲 2（接续段与 前冲1 轮切：1 -> 2 -> 1 -> 2 ...）
	UPROPERTY(EditDefaultsOnly)
	UAnimMontage* ForwardMontage2 = nullptr;
};

/**
 * 第二形态闪避特化：满足条件时不再普通闪避，而是进入居合架势。
 *
 * 触发条件分两种：
 *  - 地面：能量 >= 阈值 且 距二阶段普攻进入 Section 的窗口未过期；
 *  - 空中：浮空 + 能量 >= 阈值即可（不要求普攻窗口），落地即转入落地段。
 * 两者都照常消耗耐力，空中不额外消耗空中闪避预算。
 *
 * 居合 Montage 内的 AN 会发一个分界事件：
 *  - 分界之前按普攻 → 播放「居合前冲」并消耗能量；
 *  - 分界之后按普攻 → 交还正常 Combo GA（从第一段重新开始）。
 *
 * 前冲可连续接续：每段扣 JuheEnergyCost，扣完仍 >= 阈值时，
 * 在前冲中或 JuheForwardWindow 内再按普攻即可接下一段前冲；
 * 前冲动画在 ForwardMontage1 / ForwardMontage2 间轮切，
 * 且每次进入居合都从动画 1 起手（段序不跨激活缓存）。
 * 可触发段数由能量决定。
 * 窗口超时或能量不足即结束本 GA，之后普攻回到 Combo 第一段。
 *
 * 居合期间只额外接受一次 Dodge 输入，走基类正常后撤 Evade 动画；不参与二次闪避计数与冷却。
 * 条件不满足时完全走基类 Evade。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Evade_Juhe : public UGA_Evade
{
	GENERATED_BODY()

public:
	UGA_Evade_Juhe();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	// 是否进入居合分支：二阶段普攻开启的居合窗口未过期，且能量 >= 阈值（地面 / 空中同一套条件）
	bool ShouldEnterJuhe() const;

	// 居合期间的普攻输入：架势段接第一段前冲，前冲中/窗口内接下一段，分界后不再响应
	UFUNCTION()
	void OnJuheAttackInput(FGameplayEventData EventData);

	// 居合 Montage 内 AN 发的分界事件：此后普攻回归正常 Combo
	UFUNCTION()
	void OnJuhePhaseEnd(FGameplayEventData EventData);

	// 居合期间唯一一次 Dodge 输入：走基类正常后撤 Evade 动画
	UFUNCTION()
	void OnJuheDodgeInput(FGameplayEventData EventData);

	// 本 GA 播放的 Montage 收尾：前冲段进入接续窗口，其余段照常结束
	UFUNCTION()
	void OnJuheMontageFinished();

	// 前冲接续窗口到期
	UFUNCTION()
	void CloseJuheForwardWindow();

	// ── 空中居合：落地段（参考 GA_AirAttack_Phase2 的落地处理）──
	// 落地事件回调
	UFUNCTION()
	void OnJuheLanded(const FHitResult& Hit);

	// 落地检测兜底轮询（激活瞬间已贴地等边界情况，委托不会触发）
	void PollJuheLandCheck();

	// 进入落地段：放弃剩余接续窗口、停掉空中段，改播落地动画（播完结束 GA）
	void EnterLandPhase();

	// 空中居合收尾前的等待：仍在空中则不结束 GA（返回 true，继续等普攻或落地）；
	// 已贴地则转入落地段（同样返回 true）。非空中居合或已在落地段返回 false，由调用方正常结束。
	bool TryHoldForAirLanding();

	// 清掉落地检测（LandedDelegate + 轮询 Timer）
	void ClearJuheLandDetection();

	// 居合不参与连续闪避冷却
	virtual bool ShouldApplyDodgeCooldown() const override { return false; }

private:
	// 循环监听普攻输入（与 GA_Combo::SetupWaitComboInputPress 同款模式）
	void SetupWaitJuheAttackInput();

	// 延迟一帧再挂 Dodge 输入监听，避免触发本次激活的那个输入被新任务立即接收
	void SetupWaitJuheDodgeInput();

	// 移除居合进行中标记，放行普攻 GA（仅在确实进入过居合分支时执行）
	void RemoveJuheState();

	// 自管理 Montage 播放：切段前先解绑旧任务，避免其回调误触发收尾逻辑
	// bDodgeSegment：居合中被 Dodge 打断的后撤 Evade 段（播完直接结束 GA，与地面 Evade 一致）
	void PlayJuheMontage(UAnimMontage* Montage, bool bForwardSegment, bool bDodgeSegment = false);
	void StopJuheMontage();

	// 打出一段居合前冲：扣能量、计数、重播前冲动画并重置接续窗口
	void StartJuheForward();

	// 还能否接续下一段前冲（接续窗口开着 && 剩余能量 >= 阈值）
	bool CanChainJuheForward() const;

	// 剩余能量是否够再打一段前冲
	bool HasEnoughEnergyForJuheForward() const;

	// 本次激活使用的动画组（按 bAirJuhe 选地面 / 空中那一套）
	const FJuheMontageSet& GetActiveJuheMontages() const;

	// 取本次要播的前冲动画：每段在 前冲1 / 前冲2 间轮切，首段固定为 前冲1
	UAnimMontage* PickJuheForwardMontage() const;

	// 地面居合：架势 + 两段前冲
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	FJuheMontageSet GroundMontages;

	// 空中居合：架势 + 两段前冲
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	FJuheMontageSet AirMontages;

	// 空中居合落地时播放的落地动画（播完结束 GA）
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	UAnimMontage* AirLandMontage;

	// 进入居合所需的能量下限（>= 即可触发）
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheEnergyThreshold = 100.f;

	// 每段居合前冲消耗的能量
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheEnergyCost = 100.f;

	// 前冲后允许接续下一段前冲的窗口（秒）
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheForwardWindow = 3.f;

	// 空中居合落地检测的兜底轮询间隔
	UPROPERTY(EditDefaultsOnly, Category="Juhe|Air")
	float LandCheckInterval = 0.08f;

	// 本 GA 的 Montage 任务（自管理，前冲段播完不结束 GA）
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> JuheMontageTask;

	FTimerHandle JuheForwardWindowTimer;
	FTimerHandle JuheLandCheckTimer;

	// 本次激活是否进入了居合分支（EndAbility 兜底移除 State.Juhe 用）
	bool bEnterJuheBranch = false;

	// 本次激活是空中居合
	bool bAirJuhe = false;

	// 已进入落地段（落地只处理一次，落地动画播完才结束 GA）
	bool bInLanding = false;

	// 分界事件已触发：普攻不再接前冲
	bool bJuhePhaseEnded = false;

	// 居合中唯一一次 Dodge 已用完
	bool bJuheDodgeUsed = false;

	// 当前播放的是前冲段（收尾时据此进入接续窗口而非结束）
	bool bPlayingForwardSegment = false;

	// 当前播放的是「居合中被 Dodge 打断的后撤 Evade 段」：播完直接结束 GA
	bool bPlayingDodgeSegment = false;

	// 本次激活是否已打出过前冲（区分「架势段首次前冲」与「前冲链接续」）
	bool bJuheForwardStarted = false;

	// 本次激活已打出的前冲段数，用于 前冲1/前冲2 轮切；每次激活归零 → 首段固定为前冲1
	int32 JuheForwardIndex = 0;

	// 当前正在播前冲动画
	bool bJuheForwarding = false;

	// 前冲接续窗口开启中
	bool bJuheForwardWindowOpen = false;
};
