#pragma once

#include "CoreMinimal.h"
#include "ExtraGameplayAbility.h"
#include "GA_Combo.generated.h"

/**
 * 普攻连段GA父类：多段 Section 以 SectionName 跳转推进。
 * 默认手动节奏：进入下一段变更帧只记录 NextComboName，等下一次攻击输入再推进；
 * 「仍按住攻击键则自动续段」不下沉在此，由派生类（UGA_ComboHeavy）覆写 OnComboSectionChanged 开启。
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

protected:
	// 输入后，若 NextComboName 存在则推进到该 Section（手动点击推进；自动续段路径也复用它）
	void TryCommitCombo();

	// 进入下一段 Section 的虚钩子（调用时 NextComboName 已记录完毕）。默认空实现 = 纯手动连段；
	// 派生类覆写此处实现自动连段（仍按住攻击键则立即 TryCommitCombo）
	virtual void OnComboSectionChanged();

	// 覆写：返回当前可被移动打断的 Montage（即 ComboMontage）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return ComboMontage; }

	// 覆写：按当前 Section 选择伤害 GE（未命中 map 时 fallback 到基类 DefaultDamageEffect）
	virtual TSubclassOf<UGameplayEffect> GetDamageEffect() const override;

	// 虚钩子：基类在权威端播放连段蒙太奇后调用一次，默认空实现。
	// 派生类（UGA_ComboHeavy）在此追加注册 Montage 事件监听（末段累计 / 重击切入帧）。
	virtual void SetupComboMontageListeners();

private:
	//实现一个WaitGameplayEvent，监听 LightAttack InputTag，触发回调 HandleInputPress
	void SetupWaitComboInputPress();

	//再次实现WaitGameplayEvent处理下一次输入，形成循环，同时处理当前输入
	UFUNCTION()
	void HandleInputPress(FGameplayEventData EventData);

	//EventReceived的回调函数，找到下一个Tag的后缀，即NextComboName
	UFUNCTION()
	void ComboChangedEventReceived(FGameplayEventData InPayLoad);

	// 进入最后一段 section / 重击切入帧 Notify 的累计与判定：见派生类 UGA_ComboHeavy

	//对不同Section对应的Montage触发的DamageGE进行不同的设置
	//（Fallback到基类DefaultDamageEffect，见 GetDamageEffect 覆写）
	UPROPERTY(EditDefaultsOnly,Category="Gameplay Effect")
	TMap<FName,TSubclassOf<UGameplayEffect>> DamageEffectMap;

	//包含所有ComboAnimationSequence的Montage
	UPROPERTY(EditDefaultsOnly,Category="Animation")
	UAnimMontage* ComboMontage;

	//获得当前ComboMontage对应的下一个ComboMontage的字面量后缀，同时设置ComboSection的字面量和后缀相等
	FName NextComboName;
};
