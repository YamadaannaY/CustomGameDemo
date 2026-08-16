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
 *
 * 约束：
 *   - 一次浮空期间只允许触发一次（ActivationBlockedTags + airattack Tag）
 *   - 激活需要 character.state.airborne Tag（与 GA_Combo 靠 Tag 分流）
 *   - 空中期间锁定水平移动（SetMovementInputLocked）
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

	// Start → Loop 的交叉淡化时间（对齐原项目 0.034s）
	// 关键：让 Start 的 blend out 与 Loop 的 blend in 重叠，消除「真空帧」，
	// 否则真空帧会被状态机 idle/falling 抢占导致瞬变。
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	float StartToLoopBlendInTime = 0.034f;

	// ── 回调 ──────────────────────────────────────────────
	// 起手动画开始淡出（OnBlendOut）→ 立即进入循环下砸，与 Start 淡出重叠
	UFUNCTION()
	void OnStartMontageBlendOut();

	// 角色落地事件回调（LandedDelegate）
	UFUNCTION()
	void OnLandDetected(const FHitResult& Hit);

	// 落地检测兜底 Timer 回调
	UFUNCTION()
	void PollLandCheck();

	// 落地动画播放完毕 → 结束 GA
	UFUNCTION()
	void OnLandMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ── 辅助 ──────────────────────────────────────────────
	// 共享的落地处理逻辑（无论来自 delegate 还是 timer）
	void TryTriggerLand();

	void PlayLoopMontage();
	void PlayLandMontage();
	void StopLoopMontage();

	// 进入起手阶段（播放起手动画并绑定结束回调）
	void PlayStartMontage();

	// 覆写：移动打断时淡出的 Montage（落地动画带 cancel AN）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return AirAttackLandMontage; }

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
