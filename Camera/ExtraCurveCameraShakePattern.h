#pragma once

#include "CoreMinimal.h"
#include "Shakes/SimpleCameraShakePattern.h"
#include "ExtraCurveCameraShakePattern.generated.h"

class UCurveVector;

/**
 * 曲线驱动的相机震动 Pattern。
 *
 * 沿时间逐帧采样 UCurveVector，RGB 三通道依次作为 X/Y/Z 输出偏移。
 * Duration / BlendInTime / BlendOutTime 沿用 USimpleCameraShakePattern 的字段，
 * 淡入淡出权重由 State 计算后手动乘到结果上。
 */
UCLASS(meta = (DisplayName = "Extra Curve Camera Shake Pattern"))
class EXTRACTGAMECHARACTER_API UExtraCurveCameraShakePattern : public USimpleCameraShakePattern
{
	GENERATED_BODY()

public:
	// 震动曲线：R/G/B -> X/Y/Z
	UPROPERTY(EditAnywhere, Category = "CurveShake")
	TObjectPtr<UCurveVector> ShakeCurve;

	// 曲线值缩放：位移模式单位 cm，旋转模式单位度
	UPROPERTY(EditAnywhere, Category = "CurveShake")
	FVector CurveScale = FVector::OneVector;

	// 曲线横轴按曲线自身时间（秒）采样；勾选则把 0~Duration 归一化成 0~1 再采样
	UPROPERTY(EditAnywhere, Category = "CurveShake")
	bool bNormalizeCurveTime = false;

	// false = 输出位移偏移（cm）；true = 输出旋转偏移（度），曲线 X/Y/Z 对应绕 X/Y/Z 轴旋转
	UPROPERTY(EditAnywhere, Category = "CurveShake")
	bool bApplyToRotation = false;

protected:
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;

private:
	void SampleCurve(float Time, FCameraShakePatternUpdateResult& OutResult) const;
};
