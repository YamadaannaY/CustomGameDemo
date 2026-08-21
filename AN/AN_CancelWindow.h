#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_CancelWindow.generated.h"

/**
 * 移动输入打断的「区间」标记。
 *
 * 覆盖在 Montage 后摇段：在 NotifyBegin ~ NotifyEnd 之间，只要检测到移动输入就发送
 * ability.cancel 事件，由UExtraGameplayAbility 接收并提前结束 GA。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_CancelWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
