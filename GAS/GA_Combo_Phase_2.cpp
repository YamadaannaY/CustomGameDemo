#include "GA_Combo_Phase_2.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "ExtractGameCharacter/GAS/ExtraAbilitySystemComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_Combo_Phase_2::UGA_Combo_Phase_2()
{
	// 居合进行中不可激活普攻：居合 GA 用 loose tag 开关，AN 分界事件后移除即放行
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetJuheStateTag());

	// 命中累积寒意：每段的值配在基类 EnergyGainPerSection（按 Section 名）
	bEnableEnergyGain = true;
}

void UGA_Combo_Phase_2::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	//开启居合窗口
	if (IsActive())
	{
		OpenJuheReadyWindow();
	}
}

void UGA_Combo_Phase_2::OnComboSectionChanged()
{
	Super::OnComboSectionChanged();

	// 进入下一段：重置窗口计时
	OpenJuheReadyWindow();
}

void UGA_Combo_Phase_2::OpenJuheReadyWindow()
{
	// 窗口计时与 tag 都交给 ASC：普攻 GA 结束（实例可能被回收）后仍要按时清除
	if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
	{
		ASC->OpenJuheReadyWindow(JuheReadyWindow);
	}
}