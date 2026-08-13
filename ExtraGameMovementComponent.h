
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtraGameMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraGameMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UExtraGameMovementComponent();

	// 最大旋转速度（deg/s），用于大幅转向时（>90度）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float MaxRotationRate = 720.f;

	// 最小旋转速度（deg/s），用于接近目标朝向时的平滑减速
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float MinRotationRate = 180.f;

	// 空中（跳跃/下落）时左右输入转向的削弱系数，值越小空中越难转向（0 = 空中完全不能转向，1 = 与地面一致）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AirRotationScale = 0.3f;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	virtual void PhysicsRotation(float DeltaTime) override;
};
