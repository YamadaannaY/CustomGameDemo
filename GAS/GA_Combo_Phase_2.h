#pragma once

#include "CoreMinimal.h"
#include "GA_Combo.h"
#include "GA_Combo_Phase_2.generated.h"

/**
 * 二阶段普攻：沿用 GA_Combo 的 Section 连段节奏与按 Section 选伤害 GE 的逻辑。
 * 特化点：每次命中目标结算伤害后，额外累积寒意（EnergyValue）。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Combo_Phase_2 : public UGA_Combo
{
	GENERATED_BODY()

protected:
	// 覆写：先走基类通用伤害结算，再对「确实打到目标」的这次伤害追加累积能量
	virtual void DoDamage(const FGameplayEventData& Data) override;

	// 每次命中目标额外获得的寒意值
	UPROPERTY(EditDefaultsOnly, Category = "Energy")
	float EnergyValuePerHit = 15.f;
};
