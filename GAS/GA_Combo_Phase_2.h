#pragma once

#include "CoreMinimal.h"
#include "GA_Combo.h"
#include "GA_Combo_Phase_2.generated.h"

class UAbilitySystemComponent;

/**
 * 二阶段普攻：沿用 GA_Combo 的 Section 连段节奏与按 Section 选伤害 GE 的逻辑。
 * 特化点：每次命中目标结算伤害后，额外累积寒意（EnergyValue）。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_Combo_Phase_2 : public UGA_Combo
{
	GENERATED_BODY()

public:
	UGA_Combo_Phase_2();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	// 覆写：先走基类通用伤害结算，再对「确实打到目标」的这次伤害追加累积能量
	virtual void DoDamage(const FGameplayEventData& Data) override;

	// 覆写：进入每个 Section 时开启居合窗口（基类 UExtraGameplayAbility::OpenJuheReadyWindow）
	virtual void OnComboSectionChanged() override;

	// 每次命中目标额外获得的寒意值
	UPROPERTY(EditDefaultsOnly, Category = "Energy")
	float EnergyValuePerHit = 15.f;

	// 进入 Section 后允许触发居合的窗口时长（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Energy")
	float JuheReadyWindow = 3.f;

private:
	// 开启（刷新）居合窗口：置 State.JuheReady 计数为 1 并重置计时
	void OpenJuheReadyWindow();

	UFUNCTION()
	void ClearJuheReady();

	FTimerHandle JuheReadyTimer;

	// 窗口要跨本 GA 结束继续计时，缓存 ASC 以免 GA 结束后拿不到 ActorInfo
	TWeakObjectPtr<UAbilitySystemComponent> JuheReadyASC;
};
