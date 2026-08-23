#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_HeavyAttack.generated.h"

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_HeavyAttack : public UExtraGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_HeavyAttack();
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
private:
	UPROPERTY(EditDefaultsOnly,Category="Montage")
	UAnimMontage* HeavyAttackMontage;
};
