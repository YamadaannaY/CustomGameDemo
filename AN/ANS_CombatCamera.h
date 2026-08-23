#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"
#include "ANS_CombatCamera.generated.h"

/**
 * 战斗相机 Notify State：在 Montage 时间线上标注一段「相机意图」区间。
 *
 * Notify Begin 把 CameraRequest推入角色的CombatCameraComponent请求栈
 * Notify End 移除Request。真正的相机解算与写入由UCombatCameraComponent完成。
 * GA 的EndAbility调用ClearAllRequests兜底清理。
 */
UCLASS(meta = (DisplayName = "Combat Camera"))
class EXTRACTGAMECHARACTER_API UANS_CombatCamera : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 该区间要提交的相机意图。
	UPROPERTY(EditAnywhere, Category = "CombatCamera")
	FCombatCameraRequest CameraRequest;

private:
	// Notify Begin 推入请求后缓存其 ID，Notify End 据此移除。
	int32 CachedRequestId = INDEX_NONE;
};
