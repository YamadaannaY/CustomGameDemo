#pragma once

#include "CoreMinimal.h"
#include "ExtractGameCharacter/Projectile/ExtraProjectile.h"
#include "ExtraArrow.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * 箭矢投射物（重击弓射使用）
 * 命中即停即销毁。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraArrow : public AExtraProjectile
{
	GENERATED_BODY()

public:
	AExtraArrow();

	// 公开API供 GA 侧指定Mesh（未指定时使用蓝图配置的 mesh）
	void SetArrowMesh(UStaticMesh* InMesh);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> ArrowMeshComponent;
};
