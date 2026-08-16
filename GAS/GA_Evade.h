#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Evade.generated.h"

class AExtraPlayerCharacter;

/**
 * 闪避技能。
 * ForwardEvadeMontage 中放置 AN_EvadeToSprint Notify 在停步阶段前。
 * Notify 通过 ASC 找到本 GA 实例，调用 OnEvadeToSprint()。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Evade : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Evade();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION()
	void OnEvadeToSprint(FGameplayEventData EventData);

private:
	void PollMoveInputForSprint();
	void UpdateEvadeFacing();

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	float SprintTransitionBlendOut = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	float InputPollInterval = 0.05f;

	// -- Phase 1: AN 触发之前，左右输入调整朝向 --
	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeFacingUpdateInterval = 0.016f;

	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeMaxRotationAngle = 90.f;

	UPROPERTY(EditDefaultsOnly, Category="Evade|Facing")
	float EvadeRotationInterpSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* ForwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* BackwardEvadeMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentPlayingMontage;

	FTimerHandle InputPollTimer;
	FTimerHandle EvadeFacingTimer;
	float EvadeBaseYaw = 0.f;
	float CurrentEvadeFacingOffset = 0.f;
	bool bTransitionedToSprint = false;
	bool bIsPollingForInput = false;
};
