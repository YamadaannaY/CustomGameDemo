#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_AirAttackDive.generated.h"

/**
 * 空中下砸重击：放置在下砸动作开始的那一帧。
 * 与 RootMotion 的兼容：本 Notify 触发前，起手动画可用 RootMotion 做轻微空中位移；
 * 触发瞬间用 LaunchCharacter(..., true, true) 覆盖 velocity，清掉残余向上速度，下砸立即生效。
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
	float DiveSpeed = 8500.0f;

	// 下砸方向相对竖直方向的俯冲角（MaxTan=60）
	UPROPERTY(EditAnywhere, Category = "Dive")
	float DiveAngle = 60.0f;

	// 下砸目标相对角色前方的偏移（cm）
	UPROPERTY(EditAnywhere, Category = "Dive")
	float ForwardOffset = 250.0f;
};
