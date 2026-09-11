#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_EndUninterruptible.generated.h"

/**
 * 「不可打断结束」标记。
 *
 * 在技能Montage后摇段中放置：
 * 触发时发送 ability.uninterruptible.end 事件，由 UExtraGameplayAbility接收并
 * 从LooseTag中移除 State.Uninterruptible，从而放开对其他GA的阻断（GA默认不可在不可打断Tag存在时触发）
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_EndUninterruptible : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.uninterruptible.end（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

	virtual FString GetNotifyName_Implementation() const override;
};
