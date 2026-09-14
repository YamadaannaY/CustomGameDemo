#pragma once

#include "CoreMinimal.h"
#include "GA_Evade.h"
#include "GA_Evade_Juhe.generated.h"

class UAbilityTask_PlayMontageAndWait;

/**
 * 第二形态闪避特化：满足「能量 >= 阈值 且 距二阶段普攻进入 Section 的窗口未过期」时，
 * 不再普通闪避，而是后撤进入居合架势。
 *
 * 居合 Montage 内的 AN 会发一个分界事件：
 *  - 分界之前按普攻 → 播放「居合前冲」并消耗能量；
 *  - 分界之后按普攻 → 交还正常 Combo GA（从第一段重新开始）。
 *
 * 前冲可连续接续：每段扣 JuheEnergyCost，扣完仍 >= 阈值时，
 * 在前冲中或 JuheForwardWindow 内再按普攻即可接下一段前冲；
 * 前冲动画在 JuheForwardMontage / JuheForwardMontage2 间轮切
 * 且每次进入居合都从动画 1 起手（段序不跨激活缓存）。
 * 可触发段数由能量决定
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
	// 是否进入居合分支：能量 >= 阈值 且 二阶段普攻的居合窗口未过期（空中暂回落基类）
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

	// 居合不参与连续闪避冷却
	virtual bool ShouldApplyDodgeCooldown() const override { return false; }

	// 前冲穿身：有锁定目标时沿冲刺方向穿过目标，落在其身后 JuheForwardOvershoot 处；
	// 非前冲段（架势/后撤）只跟随朝向、不做位移 warp。
	virtual FVector ComputeLockOnWarpLocation(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget, bool& bOutWarpTranslation) const override;

	// 前冲段朝向锁定为本段起手方向；其余段沿用基类（朝目标）
	virtual FVector ComputeLockOnFaceDir(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget) const override;

private:
	// 循环监听普攻输入（与 GA_Combo::SetupWaitComboInputPress 同款模式）
	void SetupWaitJuheAttackInput();

	// 延迟一帧再挂 Dodge 输入监听，避免触发本次激活的那个输入被新任务立即接收
	void SetupWaitJuheDodgeInput();

	// 移除居合进行中标记，放行普攻 GA（仅在确实进入过居合分支时执行）
	void RemoveJuheState();

	// 自管理 Montage 播放：切段前先解绑旧任务，避免其回调误触发收尾逻辑
	void PlayJuheMontage(UAnimMontage* Montage, bool bForwardSegment);
	void StopJuheMontage();

	// 打出一段居合前冲：扣能量、计数、重播前冲动画并重置接续窗口
	void StartJuheForward();

	// 还能否接续下一段前冲（接续窗口开着 && 剩余能量 >= 阈值）
	bool CanChainJuheForward() const;

	// 剩余能量是否够再打一段前冲
	bool HasEnoughEnergyForJuheForward() const;

	// 本段前冲的 MW 落点（含 JuheForwardMaxWarpDist 上限钳制）；无目标时返回角色当前位置
	FVector ComputeJuheForwardWarpLocation(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& FallbackDir) const;

	// 前冲瞬间判定本次是否会穿过目标，并同步 State.JuhePassThrough，触发战斗镜头
	void UpdateJuhePassThroughTag(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget);

	// 前冲穿身：让角色胶囊在移动时忽略锁定目标，否则会被目标胶囊挡住
	void AddJuheForwardCollisionIgnore();
	void RemoveJuheForwardCollisionIgnore();

	// 取本次要播的前冲动画：每段在 前冲1 / 前冲2 间轮切，首段固定为 前冲1
	UAnimMontage* PickJuheForwardMontage() const;

	// 居合本段：后撤 + 居合架势 + 后摇
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	UAnimMontage* JuheMontage;

	// 居合前冲 1（每次进入居合的首段固定用它）
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	UAnimMontage* JuheForwardMontage;

	// 居合前冲 2（接续段与前冲 1 轮切：1 -> 2 -> 1 -> 2 ...）
	UPROPERTY(EditDefaultsOnly, Category="Montage|Juhe")
	UAnimMontage* JuheForwardMontage2;

	// 进入居合所需的能量下限（>= 即可触发）
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheEnergyThreshold = 100.f;

	// 每段居合前冲消耗的能量
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheEnergyCost = 100.f;

	// 前冲后允许接续下一段前冲的窗口（秒）
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheForwardWindow = 3.f;

	// 前冲穿过目标后，落点继续越过目标多远的距离（仅前冲段生效，需配 AttackFacing MW 区间）
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheForwardOvershoot = 200.f;

	// 前冲穿身的位移上限：角色当前位置到落点的最大距离，防止目标过远时瞬移过大
	UPROPERTY(EditDefaultsOnly, Category="Juhe")
	float JuheForwardMaxWarpDist = 600.f;

	// 本 GA 的 Montage 任务（自管理，前冲段播完不结束 GA）
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> JuheMontageTask;

	FTimerHandle JuheForwardWindowTimer;

	// 本次激活是否进入了居合分支（EndAbility 兜底移除 State.Juhe 用）
	bool bEnterJuheBranch = false;

	// 分界事件已触发：普攻不再接前冲
	bool bJuhePhaseEnded = false;

	// 居合中唯一一次 Dodge 已用完
	bool bJuheDodgeUsed = false;

	// 当前播放的是前冲段（收尾时据此进入接续窗口而非结束）
	bool bPlayingForwardSegment = false;

	// 本次激活是否已打出过前冲（区分「架势段首次前冲」与「前冲链接续」）
	bool bJuheForwardStarted = false;

	// 本次激活已打出的前冲段数，用于 前冲1/前冲2 轮切；每次激活归零 → 首段固定为前冲1
	int32 JuheForwardIndex = 0;

	// 当前正在播前冲动画
	bool bJuheForwarding = false;

	// 前冲接续窗口开启中
	bool bJuheForwardWindowOpen = false;

	// 本段前冲锁定的冲刺方向（起手时缓存）。穿过目标后「角色→目标」会反向，
	// 若逐帧跟随会让 warp 落点在穿越瞬间翻转 → 角色位置跳变、镜头抖动。
	FVector JuheForwardFaceDir = FVector::ZeroVector;

	// 前冲穿身期间被忽略碰撞的角色（用于结束时恢复）
	TArray<TWeakObjectPtr<AActor>> JuheIgnoredActors;
};
