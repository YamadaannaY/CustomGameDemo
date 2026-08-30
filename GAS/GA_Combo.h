#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Combo.generated.h"

/**
 * 普攻GA，连段Montage(多段Section，利用SectionName进行跳转)
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

	// 实现伤害逻辑
	UFUNCTION()
	void DoDamage(FGameplayEventData Data);

	// 服务器端收到客户端跳段通知（Server_NotifyComboCommit）后执行同一蒙太奇跳段
	UFUNCTION()
	void OnComboCommitReceived(FGameplayEventData InPayLoad);

	// 进入最后一段 section 时回调：累计「打满」次数（ComboCount +1，封顶 3）
	UFUNCTION()
	void OnLastSectionEntered(FGameplayEventData EventData);

	// 最后一段切入帧 Notify 回调：ComboCount 已满 3 且按住攻击键时，触发重击并结束当前 GA
	UFUNCTION()
	void OnHeavyTransitionFrame(FGameplayEventData EventData);

	// 攻击键是否仍按住（读取 Character 的 bHoldingAttack）
	bool IsHoldingAttack() const;

	// 本次按下是否已长按达到重击阈值（读取 Character 的 bLongPressed）
	bool IsLongPressed() const;

	// 重击所需连段次数（读取 Character 的 HeavyComboCount）
	float GetRequiredComboCount() const;

	//DamageGE
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	TSubclassOf<UGameplayEffect> DefaultDamageEffect;

	//对不同Section对应的Montage触发的DamageGE进行不同的设置
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	TMap<FName,TSubclassOf<UGameplayEffect>> DamageEffectMap;

	// SetByCaller 伤害 Tag：DoDamage 将攻击者攻击力写入该 Tag，供 GE 的 Modifier 读取
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	FGameplayTag DamageSetByCallerTag;

	//包含所有ComboAnimationSequence的Montage
	UPROPERTY(EditDefaultsOnly,Category="Animation")
	UAnimMontage* ComboMontage;

	//获得当前ComboMontage对应的下一个ComboMontage的字面量后缀，同时设置ComboSection的字面量和后缀相等
	FName NextComboName;

protected:
	// 覆写：返回当前可被移动打断的 Montage（即 ComboMontage）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return ComboMontage; }
};