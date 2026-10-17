#include "GA_Combo_Phase_2.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "Engine/World.h"
#include "TimerManager.h"

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
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	JuheReadyASC = ASC;

	// 用 SetLooseGameplayTagCount 置 1 而非 AddLooseGameplayTag：后者是计数累加语义，
	ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 1);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(JuheReadyTimer, this, &ThisClass::ClearJuheReady, JuheReadyWindow, false);
	}
}

void UGA_Combo_Phase_2::ClearJuheReady()
{
	if (UAbilitySystemComponent* ASC = JuheReadyASC.Get())
	{
		ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetJuheReadyStateTag(), 0);
	}
}