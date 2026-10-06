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
	// 推进到下一段：按本端已解析出的 NextComboName 跳本端这份 Montage。
	// Montage 的 Section 不参与网络复制——各端播的是各自那份 Montage，所以这里不做跨端通信，
	// 只要求各端都收到同一次输入即可（由 ASC 的输入复制通道保证），
	// 收到后各端各自跳段，两端自然一致。
	void TryCommitCombo();

	// 虚钩子：本次连段的起始 Section 名（默认 NAME_None = 从蒙太奇首段起播）。
	// 派生类覆写以指定起始段（形态一：Skill01 强化后直跳末段）
	virtual FName GetComboStartSectionName();

	// ComboMontage 最后一段的 Section 名
	FName GetLastComboSectionName() const;

	// 进入下一段 Section 的虚钩子（调用时 NextComboName 已记录完毕）。默认空实现 = 纯手动连段；
	// 派生类覆写此处实现自动连段（仍按住攻击键则重发一次轻击输入）
	virtual void OnComboSectionChanged();

	// 覆写：返回当前可被移动打断的 Montage（即 ComboMontage）
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return ComboMontage; }

	// 覆写：按当前 Section 选择伤害 GE（未命中 map 时 fallback 到基类 DefaultDamageEffect）
	virtual TSubclassOf<UGameplayEffect> GetDamageEffect() const override;

	// 基类在权威端播放连段蒙太奇后调用一次，默认空实现。
	virtual void SetupComboMontageListeners();

private:
	//挂载本 GA 输入按下事件的监听，收到即处理并续上下一次
	void SetupWaitComboInputPress();

	//本 GA 的输入按下回调，处理完成后重新挂载，形成循环
	UFUNCTION()
	void HandleInputPress(float TimeWaited);

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
