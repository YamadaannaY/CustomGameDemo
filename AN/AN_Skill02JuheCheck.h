#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_Skill02JuheCheck.generated.h"

/**
 * Skill_02「长按技能进居合」的检测帧。
 *
 * 放在两处 Montage 的「到这里还没松手就允许进居合」那一帧：
 *  - 段1（升空斩）：此时人在空中 → GA_Evade_Juhe 自己会判成空中居合
 *  - 落地斩的 Land 段：此时已落地 → 判成地面居合
 *
 * 触发时发送 ability.skill02.juhecheck，UGA_Skill_02 收到后判定
 * 「E 键仍按住 + 能量足够」，满足则挂 JuheReady 并发一次闪避输入，
 * 由 GA_Evade_Juhe 走它原本的进居合路径。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_Skill02JuheCheck : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.skill02.juhecheck（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

	virtual FString GetNotifyName_Implementation() const override;
};
