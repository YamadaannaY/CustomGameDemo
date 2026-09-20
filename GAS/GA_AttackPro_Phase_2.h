#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_AttackPro_Phase_2.generated.h"

/**
 * 二阶段爆发重击（AttackPro）。
 *
 * 二阶段内打出三次「居合」前冲段（空中 / 地面都算
 * 计数在 GA_Evade_Juhe::StartJuheForward 里做自增，架势段不计入）
 * → 挂上 State.ProReady → 长按普攻达阈值时由角色分派到 InputTag.AttackPro 触发GA。
 *
 * 「整个二阶段只能触发一次」：激活即消耗 State.ProReady为0，而该标记只在
 * 离开二阶段时才被清掉，所以要再次触发必须回一阶段、再进二阶段、重新打满三次居合。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_AttackPro_Phase_2 : public UExtraGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_AttackPro_Phase_2();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UPROPERTY(EditDefaultsOnly,Category="Animation")
	UAnimMontage* AttackBurstProMontage ;
};
