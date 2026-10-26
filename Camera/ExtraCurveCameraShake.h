#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ExtraCurveCameraShake.generated.h"

class UExtraCurveCameraShakePattern;

/**
 * 曲线驱动的相机震动资产基类。
 * 构造时挂载 UExtraCurveCameraShakePattern；蓝图子类里在 Camera Shake 分类下
 * 展开 RootShakePattern 填曲线、时长与缩放。触发仍走 UAN_CameraShake。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Extra Curve Camera Shake"))
class EXTRACTGAMECHARACTER_API UExtraCurveCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UExtraCurveCameraShake(const FObjectInitializer& ObjectInitializer);
};
