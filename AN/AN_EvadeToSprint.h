#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_EvadeToSprint.generated.h"

/**
 * 放置在 ForwardEvadeMontage 停步减速阶段之前。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_EvadeToSprint : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
	
};
