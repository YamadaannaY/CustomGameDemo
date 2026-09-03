#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtraGameMovementComponent.generated.h"

/*
 *项目用CMC
 * -重写了转向逻辑，恒定速率（RotationRate.Yaw），空中额外削弱
 *
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraGameMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UExtraGameMovementComponent();

	// 空中（跳跃/下落）时左右输入转向的削弱系数，值越小空中越难转向（0 = 完全不能转向，1 = 与地面一致）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AirRotationScale = 0.3f;

	// 转向：恒定 RotationRate.Yaw，空中按 AirRotationScale 削弱
	virtual void PhysicsRotation(float DeltaTime) override;
};
