#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_EvadeToSprint.generated.h"

/**
 * 放置在 ForwardEvadeMontage 停步减速阶段之前。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_EvadeToSprint : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.evade.sprint（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
