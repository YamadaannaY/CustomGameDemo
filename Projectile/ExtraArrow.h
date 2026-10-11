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

	virtual void BeginPlay() override;

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// 公开API供 GA 侧指定Mesh
	void SetArrowMesh(UStaticMesh* InMesh);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> ArrowMeshComponent;

	// 箭身网格：由 GA 在权威端注入（SetArrowMesh）后复制给客户端。
	UPROPERTY(ReplicatedUsing = OnRep_ArrowMesh)
	TObjectPtr<UStaticMesh> ArrowMesh;

	UFUNCTION()
	void OnRep_ArrowMesh();

private:
	// 把 ArrowMesh 应用到组件
	void ApplyArrowMesh();
};
