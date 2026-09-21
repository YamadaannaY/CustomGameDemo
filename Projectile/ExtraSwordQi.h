#pragma once

#include "CoreMinimal.h"
#include "ExtractGameCharacter/Projectile/ExtraProjectile.h"
#include "ExtraSwordQi.generated.h"

class UBoxComponent;
class UNiagaraComponent;

/**
 * 剑气
 *
 * 飞行与 GE 结算沿用 AExtraProjectile；自身特点：
 *  - 穿透：命中敌人只结算、继续飞，撞到静态几何才停下销毁
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraSwordQi : public AExtraProjectile
{
	GENERATED_BODY()

public:
	AExtraSwordQi();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SwordQi")
	TObjectPtr<UBoxComponent> CollisionBox;

	// 剑气特效；具体粒子在蓝图子类里指定
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SwordQi")
	TObjectPtr<UNiagaraComponent> SlashNiagaraEffect;
};
