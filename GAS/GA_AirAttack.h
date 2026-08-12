#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_AirAttack.generated.h"

/**
 * 空中普攻：在空中按下轻击 → 播放落地攻击 Montage → 落地后结束
 * Activation 需要 character.state.airborne Tag。
 * 与 GA_Combo 共用 InputID=LightAttack，由 Tag 过滤自动路由。
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
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AirAttackMontage;

	UFUNCTION()
	void OnMontageBlendOut();
	
	UFUNCTION()
	void OnMontageCancelled();
};
