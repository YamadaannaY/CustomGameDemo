#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_GravityScale.generated.h"

class USkeletalMeshComponent;

/**
 * 重力系数窗口：区间内把 CharacterMovement 的 GravityScale 覆写为「曲线值」，出窗恢复为进入前的值。
 * 实现重力在窗口内曲线渐变
 * 若留空 GravityCurveName 则整段使用 GravityScale 常量
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UANS_GravityScale : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

protected:
	// 重力曲线名（Montage 上的 Float Curve）；留空则整段使用 GravityScale
	UPROPERTY(EditAnywhere, Category = "Gravity")
	FName GravityCurveName;

	// 未配置曲线时的重力系数；0 = 无重力（匀速下落），1 = 引擎默认
	UPROPERTY(EditAnywhere, Category = "Gravity")
	float GravityScale = 1.0f;

	// 出窗时是否把重力恢复为进窗前的值。
	// 连段场景（多个 Montage 顺序播、每段各挂一个本 ANS）必须关掉：
	// 上一段的 NotifyEnd 要等它淡出结束才到达，会在下一段窗口期间把刚从曲线写进去的重力覆盖回旧值。
	UPROPERTY(EditAnywhere, Category = "Gravity")
	bool bRestoreOnEnd = true;

	// 进窗时是否清零垂直速度。
	// 重力归零只停止「继续加速」，此前累积的下落速度不会消失（角色会匀速继续下坠）；
	UPROPERTY(EditAnywhere, Category = "Gravity")
	bool bResetVerticalVelocityOnBegin = false;

private:
	// 计算本次要写入的重力系数
	float EvaluateGravity(const USkeletalMeshComponent* MeshComp) const;

	// 按当前值（曲线或参数）覆写 GravityScale
	void ApplyGravity(USkeletalMeshComponent* MeshComp) const;
	
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> CachedGravityScale;
};
