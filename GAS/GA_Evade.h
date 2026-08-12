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
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION()
	void OnEvadeToSprint(FGameplayEventData EventData);

private:
	void OnEvadeMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	float SprintTransitionBlendOut = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* ForwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* BackwardEvadeMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentPlayingMontage;

	bool bTransitionedToSprint = false;
};
