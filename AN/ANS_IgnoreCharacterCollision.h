#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_IgnoreCharacterCollision.generated.h"

/**
 * 穿透碰撞 Notify State：区间内让 Owner 与场上其他 AExtraCharacter 互相忽略移动抓取，
 * 使带位移的动画（如居合前冲的穿身、绕圈回原点的斩击）能穿过对方的胶囊而不被挡住。
 */
UCLASS(meta = (DisplayName = "Ignore Character Collision"))
class EXTRACTGAMECHARACTER_API UANS_IgnoreCharacterCollision : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
