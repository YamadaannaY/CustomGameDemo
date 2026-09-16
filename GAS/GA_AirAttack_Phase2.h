#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ExtraGameplayAbility.h"
#include "GA_AirAttack_Phase2.generated.h"

class UAnimMontage;

/**
 * 二阶段空中连斩：一次激活内最多连打三段，动画严格 1→2→1。
 *
 * 落地播放Land动画。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_AirAttack_Phase2 : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_AirAttack_Phase2();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 首段用动画1
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AttackMontage1;

	//第二段
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AttackMontage2;

	// 落地段：落地帧播放
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandMontage;

	// 落地检测兜底轮询间隔
	UPROPERTY(EditDefaultsOnly, Category = "Land")
	float LandCheckInterval = 0.08f;

	// 当前推进到第几段（0 起）
	int32 StageIndex = 0;

	// 三段封顶：动画 1 → 2 → 1
	static constexpr int32 MaxComboStages = 3;

	// 当前是否处于可衔接窗口内（由 AN_AttackComboWindow 的开/关事件切换）
	bool bComboWindowOpen = false;

	// 正在主动切段：抑制上一段 Montage 的 Interrupted 回调
	bool bTransitioning = false;

	// 已进入落地段：落地检测只触发一次，落地段结束后才结束 GA
	bool bInLanding = false;

	// 当前在播的段
	UPROPERTY()
	UAnimMontage* CurrentPlayingMontage = nullptr;

	FTimerHandle LandCheckTimerHandle;

	// 播放第 InIndex
	void PlayStage(int32 InIndex);

	// 窗口内输入：推进到下一段
	void AdvanceToNextStage();

	// 停掉当前段（调用前先置 bTransitioning）
	void StopCurrentPlayingMontage();

	UAnimMontage* GetMontageForStage(int32 InIndex) const;

	// 进落地段：停掉空中段、改播 LandMontage
	void EnterLandPhase();

	// 清掉落地检测（LandedDelegate + 轮询 Timer），进入落地段或结束 GA 时调用
	void ClearLandDetection();

	// 覆写：空中 / 落地段被移动打断时，停掉当前在播的那一段
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return CurrentPlayingMontage; }

	// 进窗 
	UFUNCTION()
	void OnComboWindowBegin(FGameplayEventData Payload);
	
	//出窗
	UFUNCTION()
	void OnComboWindowEnd(FGameplayEventData Payload);

	// 输入：仅在窗口内、且未打满三段时推进
	UFUNCTION()
	void OnLightAttackInput(FGameplayEventData Payload);

	// 本段自然播完（窗口内不输入）→ 结束 GA
	UFUNCTION()
	void OnStageMontageCompleted();

	// 本段被外部打断（如闪避抢占同 slot）→ 结束 GA；主动切段时 bTransitioning 已置位，直接忽略
	UFUNCTION()
	void OnStageMontageInterrupted();

	// ── 落地 ──────────────────────────────────────────────
	UFUNCTION()
	void OnLanded(const FHitResult& Hit);

	void PollLandCheck();

	// 落地段播完 / 被打断 → 结束 GA
	UFUNCTION()
	void OnLandMontageFinished();
};