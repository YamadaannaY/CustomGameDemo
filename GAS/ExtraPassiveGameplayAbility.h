#pragma once

#include "CoreMinimal.h"
#include "ExtraAbilityBase.h"
#include "ExtraPassiveGameplayAbility.generated.h"

/**
 * 被动 GA 基类。
 */
UCLASS(Abstract)
class EXTRACTGAMECHARACTER_API UExtraPassiveGameplayAbility : public UExtraAbilityBase
{
	GENERATED_BODY()

public:
	UExtraPassiveGameplayAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:

	virtual void OnReactActivated(const FGameplayEventData* TriggerEventData) {}
};
