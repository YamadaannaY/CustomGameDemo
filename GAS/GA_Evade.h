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
	void HandleDodgeInputPress(FGameplayEventData EventData);

	// 空中 Evade 落地回调：落地立即结束 GA（空中闪避最终必然落地，落地时 montage 可能仍未播完）
	UFUNCTION()
	void OnAirEvadeLandDetected(const FHitResult& Hit);

private:
	// 挂载 Dodge 输入监听（GameplayEvent 方式，类似 GA_Combo 循环监听 LightAttack）
	void SetupWaitDodgeInputPress();
	void PlayEvadeMontage();

	// 播放/重播当前 Montage，并接管旧 Montage 被替换时触发的任务回调
	void HandlePlayMontageTaskDelegates(UAbilityTask_PlayMontageAndWait* Task);

	// 当前激活对应的 PlayMontage 任务（重播时先 EndTask 旧任务，避免其 OnInterrupted 误杀 GA）
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayEvadeMontageTask;

	void PollMoveInputForSprint();
	void UpdateEvadeFacing();

	// 将当前 EvadeBaseYaw + CurrentEvadeFacingOffset 写入 MotionWarp target。
	void ApplyEvadeFacingWarp(const AExtraPlayerCharacter* PlayerChar);
	
	UPROPERTY(EditDefaultsOnly, Category="Montage")
	float InputPollInterval = 0.05f;

	//AN 触发之前，MotionWarp左右输入调整朝向的应用间隔
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeFacingUpdateInterval = 0.016f;

	//左右输入调整朝向可以达到的最大角度
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeMaxRotationAngle = 90.f;

	//旋转每帧插值速度
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeRotationInterpSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* ForwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* BackwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage|Air")
	UAnimMontage* ForwardAirEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage|Air")
	UAnimMontage* BackwardAirEvadeMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentPlayingMontage;

	// 本次激活选择的是前冲（Forward/ForwardAir），用于朝向调整等前冲专属逻辑
	bool bPlayingForwardEvade = false;

	// 本次激活为空中 Evade（已绑定 LandedDelegate，EndAbility 时解绑）
	bool bAirborneEvade = false;

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
	// EvadeToSprint 通知已触发：此后不再响应再次闪避
	bool bEvadeToSprintTriggered = false;
	
	UPROPERTY(EditDefaultsOnly, Category="Cooldown")
	TSubclassOf<UGameplayEffect> MaxDodgeTriggerCooldownEffect;
};
