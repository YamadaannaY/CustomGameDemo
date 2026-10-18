#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayCueNotify_DamageNumber.generated.h"

class ADamageNumberActor;

/**
 * 伤害飘字的 GameplayCue（Executed 型）。
 * 服务端结算伤害后通过 GameplayCueManager 广播，各端本地执行 OnExecute 生成自己的飘字 Actor
 * （飘字是纯本地表现，本身不复制）。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGameplayCueNotify_DamageNumber : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	// 飘字 Actor 类
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber")
	TSubclassOf<ADamageNumberActor> DamageNumberActorClass;

	// 飘字生成位置：目标 Mesh 上的骨骼（没有该骨骼时选择被命中者位置）
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber")
	FName AttachBoneName = TEXT("head");

	// 在骨骼位置基础上再抬高的偏移（cm）
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber")
	float SpawnHeightOffset = 20.f;
};
