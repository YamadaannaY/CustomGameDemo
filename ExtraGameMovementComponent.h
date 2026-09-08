#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtraGameMovementComponent.generated.h"

/*
 *项目用CMC
 * -重写了转向逻辑：地面转向速率随角度差自适应（小角度慢速微调/大角度快速掉头，可开关），空中额外削弱
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

	// ── 角度差自适应转向速率（仅地面生效）──────────────
	// 总开关：false 时退回恒定 RotationRate.Yaw（旧逻辑）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)")
	bool bEnableAdaptiveTurnRate = true;

	// 角度差 <= 此值（度）视为微调带：速率从 SlowTurnMinRate 线性升到 RotationRate.Yaw，
	// 用于轻微修正方向时柔和回正、避免甩头
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="180.0"))
	float SlowTurnAngle = 45.f;

	// 微调带最小转向速率（角度差 0° 时）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float SlowTurnMinRate = 280.f;

	// 角度差 >= 此值（度）进入快转带：速率从 RotationRate.Yaw 线性升到 FastTurnMaxRate，用于大角度快速掉头
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0", ClampMax="180.0"))
	float FastTurnAngle = 100.f;

	// 快转带最大转向速率（角度差 180° 时）
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation Settings)", meta=(ClampMin="0.0"))
	float FastTurnMaxRate = 900.f;

	// 转向：地面按角度差自适应速率，空中按 AirRotationScale 削弱
	virtual void PhysicsRotation(float DeltaTime) override;

private:
	// 由角度差（0..180 度）线性映射出本帧转向速率（deg/s）
	float GetAdaptiveTurnRate(float AbsDeltaYaw, float BaseYawRate) const;
};
