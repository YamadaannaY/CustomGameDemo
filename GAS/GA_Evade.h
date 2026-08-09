#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Evade.generated.h"

/**
 *
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Evade : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:

    void OnEvadeMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	
	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* ForwardEvadeMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montage")
	UAnimMontage* BackwardEvadeMontage;

	// 当前播放的 Montage，EndAbility 时用于停止
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentPlayingMontage;
};
