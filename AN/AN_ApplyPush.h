#pragma once

#include "CoreMinimal.h"
#include "AN_SendGameplayEvent.h"
#include "AN_ApplyPush.generated.h"

/**
 * 对角色应用一个推力。
 * 发送 Push_Self Tag，附带 FPushTargetData（推力向量 + 覆盖标志），GA监听此并读取Data
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_ApplyPush : public UAN_SendGameplayEvent
{
	GENERATED_BODY()

public:
	// 固定发送 ability.push.self
	virtual FGameplayTag GetEventTag() const override;

	virtual FString GetNotifyName_Implementation() const override;

	// 施加的推力向量（cm/s）。默认向上。
	UPROPERTY(EditAnywhere, Category = "Push")
	FVector PushVelocity = FVector(0.0f, 0.0f, 600.0f);

	// 是否覆盖水平（XY）速度。
	UPROPERTY(EditAnywhere, Category = "Push")
	bool bOverrideXY = false;

	// 是否覆盖竖直（Z）速度。
	UPROPERTY(EditAnywhere, Category = "Push")
	bool bOverrideZ = true;

protected:
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) override;
};
