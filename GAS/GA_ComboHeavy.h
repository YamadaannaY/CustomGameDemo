#pragma once

#include "CoreMinimal.h"
#include "GA_Combo.h"
#include "GA_ComboHeavy.generated.h"

/**
 * 形态一特写普攻GA（按住自动连段 + 长按重击）：
 * 在父类 UGA_Combo 之上开启自动续段（覆写 OnComboSectionChanged），并注册
 * 「进入最后一段累计能量」与「重击切入帧判定」：累计 EnergyValue 至要求值后，
 * 仍按住攻击键并长按时，发送重击输入并结束自身GA，激活重击GA。
 * 第二阶段 Combo 复用父类 UGA_Combo，不具自动连段。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_ComboHeavy : public UGA_Combo
{
	GENERATED_BODY()

public:
	UGA_ComboHeavy();

protected:
	// 覆写基类虚钩子：权威端播放连段蒙太奇后，注册末段累计 / 重击切入帧监听
	virtual void SetupComboMontageListeners() override;

	// 覆写：进入下一段 Section（NextComboName 已记录），仍按住攻击键则自动续段
	virtual void OnComboSectionChanged() override;

private:
	// 攻击键是否仍按住（读取 Character 的 bHoldingAttack；按住时本形态自动续段）
	bool IsHoldingAttack() const;

	// 进入最后一段 section 时回调：累计能量（EnergyValue +100，封顶 HeavyComboMaxVal）
	UFUNCTION()
	void OnLastSectionEntered(FGameplayEventData EventData);

	// 最后一段切入帧 Notify 回调：EnergyValue 打满要求值且长按达标时，触发重击并结束当前 GA
	UFUNCTION()
	void OnHeavyTransitionFrame(FGameplayEventData EventData);

	// 本次按下是否已长按达到重击阈值（读取 Character 的 bLongPressed）
	bool IsLongPressed() const;

	// 重击所需能量值（读取 Character 的 HeavyComboMaxVal）
	float GetRequiredComboCount() const;
};
