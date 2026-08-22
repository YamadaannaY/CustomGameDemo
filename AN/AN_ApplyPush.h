#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_ApplyPush.generated.h"

/**
 * 在动画帧上对角色施加一个推力（LaunchCharacter）。
 *
 * 不再直接查找当前 GA（LocalPredicted 下 GetPrimaryInstance 不可靠），
 * 改为发送 Push_Self GameplayEvent，附带推力向量 + 覆盖标志（通过 FPushTargetData 承载），
 * 由 UExtraGameplayAbility 在 PreActivate 统一挂载的监听接收并调用 PushSelf。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_ApplyPush : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 施加的推力向量（cm/s）。默认向上。
	UPROPERTY(EditAnywhere, Category = "Push")
	FVector PushVelocity = FVector(0.0f, 0.0f, 600.0f);

	// 是否覆盖水平（XY）速度。二段跳应保持 false，避免清掉原有的水平移动。
	UPROPERTY(EditAnywhere, Category = "Push")
	bool bOverrideXY = false;

	// 是否覆盖竖直（Z）速度。二段跳应保持 true，直接用推力替换当前竖直速度。
	UPROPERTY(EditAnywhere, Category = "Push")
	bool bOverrideZ = true;
};
