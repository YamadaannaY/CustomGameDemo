#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_AirAttackDive.generated.h"

/**
 * 空中下砸重击：通过LaunchCharacter施加一个特定俯仰角的速度
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_AirAttackDive : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 下砸俯冲速度（cm/s）
	UPROPERTY(EditAnywhere, Category = "Dive")
	float DiveSpeed = 4500.0f;

	// 下砸方向相对竖直方向的俯冲角
	UPROPERTY(EditAnywhere, Category = "Dive")
	float DiveAngle = 60.0f;
};
