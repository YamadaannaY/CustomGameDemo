// Fill out your copyright notice in the Description page of Project Settings.

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
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	//获得ComboChange下所有具体的comboTag
	static FGameplayTag GetComboChangedEventTag();
	//获得ComboChange下的endTag
	static FGameplayTag GetComboChangedEventEndTag();
	//获得TargetEvent对应的DamageTag
	static FGameplayTag GetComboTargetEventTag();
	
	static FGameplayTag GetRecoveryCancelTag();

private:
	//实现一个WaitInputPressTask，绑定触发输入后的回调HandleInputPress
	void SetupWaitComboInputPress();

	//再次实现WaitInputPressTask处理下一次输入，形成循环，同时处理当前输入
	UFUNCTION()
	void HandleInputPress(float TimeWaited);

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
	// 后摇 Notify 触发时的回调（通过 AnimNotify 发送 EventTag 触发）
	UFUNCTION()
	void OnRecoveryCancelNotifyReceived(FGameplayEventData Payload);

	// 检查玩家是否有 WASD 移动输入
	bool HasMovementInput() const;

	// 定时检查移动输入的 Timer
	FTimerHandle MovementCheckTimerHandle;
	void CheckMovementInputForCancel();
};