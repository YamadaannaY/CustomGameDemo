#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Combo.generated.h"

/**
 *
 */
UCLASS()
class  EXTRACTGAMECHARACTER_API UGA_Combo : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Combo();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	//获得ComboChange下所有具体的comboTag
	static FGameplayTag GetComboChangedEventTag();
	//获得ComboChange下的endTag
	static FGameplayTag GetComboChangedEventEndTag();
	//获得TargetEvent对应的DamageTag
	static FGameplayTag GetComboTargetEventTag();

private:
	//实现一个WaitGameplayEvent，监听 LightAttack InputTag，触发回调 HandleInputPress
	void SetupWaitComboInputPress();

	//再次实现WaitGameplayEvent处理下一次输入，形成循环，同时处理当前输入
	UFUNCTION()
	void HandleInputPress(FGameplayEventData EventData);

	//输入后，若NextComboName存在，则设置NextSection为这个Name对应的Section
	void TryCommitCombo();

	//找到当前Section对应的DamageGE
	TSubclassOf<UGameplayEffect> GetDamageEffectForCurrentCombo() const ;

	//EventReceived的回调函数，找到下一个Tag的后缀，即NextComboName
	UFUNCTION()
	void ComboChangedEventReceived(FGameplayEventData InPayLoad);

	//实现伤害逻辑
	UFUNCTION()
	void DoDamage(FGameplayEventData Data);

	//DamageGE
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	TSubclassOf<UGameplayEffect> DefaultDamageEffect;

	//对不同Section对应的Montage触发的DamageGE进行不同的设置
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	TMap<FName,TSubclassOf<UGameplayEffect>> DamageEffectMap;

	//包含所有ComboAnimationSequence的Montage
	UPROPERTY(EditDefaultsOnly,Category="Animation")
	UAnimMontage* ComboMontage;

	//获得当前ComboMontage对应的下一个ComboMontage的字面量后缀，同时设置ComboSection的字面量和后缀相等
	FName NextComboName;

protected:
	// 覆写：返回当前可被移动打断的 Montage（即 ComboMontage）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return ComboMontage; }
};