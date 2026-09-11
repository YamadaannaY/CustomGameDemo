#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_ApplyPush.generated.h"

/**
 * 在动画帧上对角色施加一个推力（LaunchCharacter）。
 * 发送 Push_Self GameplayEvent（固定 ability.push.self），附带 FPushTargetData（推力向量 + 覆盖标志），
 * 由 GA基类 在 PreActivate 统一挂载的监听接收并调用 PushSelf。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_ApplyPush : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.push.self（硬编码，面板不暴露 EventTag）
	virtual FGameplayTag GetEventTag() const override;

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

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
