#pragma once

#include "CoreMinimal.h"

#if WITH_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerCategory.h"

/**
 * Combat Camera 的 Gameplay Debugger 分类（PIE 内按 ` 键展开）。
 *
 * 显示当前战斗相机状态（位置/旋转偏移、臂长、FOV、生效请求），
 * 并在 3D 世界里画出相机实际位置与视线方向，便于在动画编辑器外实时验证相机效果。
 */
class EXTRACTGAMECHARACTER_API FCombatCameraDebuggerCategory : public FGameplayDebuggerCategory
{
public:
	FCombatCameraDebuggerCategory();

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;
};

#endif // WITH_GAMEPLAY_DEBUGGER
