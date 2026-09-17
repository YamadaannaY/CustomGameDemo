#pragma once

#include "CoreMinimal.h"
#include "ExtractGameCharacter/Projectile/ExtraProjectile.h"
#include "ExtraArrow.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * 箭矢投射物（重击弓射使用）
 *
 * 飞行、命中判定与 GE 结算都沿用 AExtraProjectile；本类只负责箭矢自己的部分：
 * 细碰撞球 + 箭身 mesh（纯视觉）。命中即停即销毁。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraArrow : public AExtraProjectile
{
	GENERATED_BODY()

public:
	AExtraArrow();

	// 若需由 GA 侧指定箭身 mesh（未指定时使用蓝图配置的 mesh）
	void SetArrowMesh(UStaticMesh* InMesh);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> ArrowMeshComponent;
};
