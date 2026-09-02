#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "GA_AirAttack.generated.h"

/**
 * 空中下落重击：角色在空中（跳跃或下落）按下轻击 → 触发一次空中攻击。
 *
 * 流程（三段动画）：
 *   1. 起手动画 AirAttackStartMontage 播完（OnCompleted）
 *   2. 循环下砸动画 AirAttackLoopMontage，直到落地
 *   3. 落地动画 AirAttackLandMontage，播完结束 GA
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_AirAttack : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_AirAttack();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// ── 动画 ──────────────────────────────────────────────
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AirAttackStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AirAttackLoopMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AirAttackLandMontage;

	// Loop 落地瞬间，Loop → Land 的 blend 时间
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	float LoopToLandBlendInTime = 0.15f;

	// Start → Loop 的交叉淡化时间
	// 关键：让 Start 的 blend out 与 Loop 的 blend in 重叠，消除「真空帧」，
	// 否则真空帧会被状态机 idle/falling 抢占导致瞬变。
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	float StartToLoopBlendInTime = 0.034f;

	// 起手动画开始淡出（OnBlendOut）→ 立即进入循环下砸，与 Start 淡出重叠
	UFUNCTION()
	void OnStartMontageBlendOut();

	// 起手动画被中断：仅在 Start 阶段（真正外部打断）才结束 GA；
	// 进入 Loop/Land 后主动停止 Start 属于正常流程，不应结束 GA。
	UFUNCTION()
	void OnStartMontageInterrupted();

	// 角色落地事件回调（LandedDelegate）
	UFUNCTION()
	void OnLandDetected(const FHitResult& Hit);

	// 落地检测兜底 Timer 回调
	UFUNCTION()
	void PollLandCheck();

	// 落地动画播放完毕 → 结束 GA
	UFUNCTION()
	void OnLandMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// Loop montage 被外部打断（如空中 Evade 抢占同 slot）→ 结束 GA。
	// 主动停 Loop 进 Land（PlayLandMontage）或 EndAbility 兜底停时 CurrentPhase 已离开 Loop，回调直接 return。
	UFUNCTION()
	void OnLoopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 共享的落地处理逻辑（无论来自 delegate 还是 timer）
	void TryTriggerLand();

	void PlayLoopMontage();
	void PlayLandMontage();
	void StopLoopMontage();

	// 进入起手阶段（播放起手动画并绑定结束回调）
	void PlayStartMontage();

	// 覆写：移动打断时淡出的 Montage（落地动画带 cancel AN）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return AirAttackLandMontage; }

	// 命中移动打断瞬间：立即清掉 AnimBP 让位标记，让状态机在 montage 停止的同一帧接管
	virtual void OnMovementCancelTriggered() override;

	// 记录当前处于哪个阶段，用于 EndAbility 清理
	enum class EAirAttackPhase : uint8
	{
		None,
		Start,
		Loop,
		Land
	};
	
	EAirAttackPhase CurrentPhase = EAirAttackPhase::None;

	// 落地检测兜底 Timer 句柄
	FTimerHandle LandCheckTimerHandle;
};
