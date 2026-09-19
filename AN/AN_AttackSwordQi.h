#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_AttackSwordQi.generated.h"

/**
 * 空中攻击「斩出剑气」帧事件。
 *
 * 固定发送 ability.airattack.swordqi并把本 AN 上配置的
 * 倾斜角经 EventMagnitude 随事件发给 GA —— 于是每道剑气的朝向由它所在的这一帧决定，
 * 而飞行方向仍由 GA 统一算，不受影响。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_AttackSwordQi : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	virtual FGameplayTag GetEventTag() const override;

	virtual FString GetNotifyName_Implementation() const override;

	// 剑气绕飞行轴的滚转角（度）：只改朝向不改方向，左右翻靠正负号
	UPROPERTY(EditAnywhere, Category = "SwordQi")
	float RollAngle = 0.f;

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
