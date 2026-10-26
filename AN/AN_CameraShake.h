#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_CameraShake.generated.h"

class UCameraShakeBase;
class USkeletalMeshComponent;

/**
 * 单点相机震动 Notify：触发瞬间播一次 CameraShake。
 *
 * 纯本地表现，服务端不播（服务端执行 ClientStartCameraShake 会让客户端重复收到一次，震动翻倍）。
 * bForSelf=true 只震该角色对应的本地玩家；
 * bForSelf=false 走世界范围播放，按距离衰减（全屏震动）。
 */
UCLASS(meta = (DisplayName = "Camera Shake"))
class EXTRACTGAMECHARACTER_API UAN_CameraShake : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 震动资产（软引用，判定通过后再加载）
	UPROPERTY(EditAnywhere, Category = "CameraShake")
	TSoftClassPtr<UCameraShakeBase> ShakeClass;

	// 强度倍数，叠加在震动资产自身振幅上；仅 bForSelf 模式生效
	UPROPERTY(EditAnywhere, Category = "CameraShake", meta = (ClampMin = "0.0"))
	float Scale = 1.f;

	// true = 只震该角色本地的玩家；false = 世界范围震动，按距离衰减
	UPROPERTY(EditAnywhere, Category = "CameraShake")
	bool bForSelf = true;

	UPROPERTY(EditAnywhere, Category = "CameraShake|World", meta = (EditCondition = "!bForSelf", ClampMin = "0.0"))
	float InnerRadius = 800.f;

	UPROPERTY(EditAnywhere, Category = "CameraShake|World", meta = (EditCondition = "!bForSelf", ClampMin = "0.0"))
	float OuterRadius = 3000.f;

	UPROPERTY(EditAnywhere, Category = "CameraShake|World", meta = (EditCondition = "!bForSelf", ClampMin = "0.0"))
	float Falloff = 1.f;

	UPROPERTY(EditAnywhere, Category = "CameraShake|World", meta = (EditCondition = "!bForSelf"))
	bool bOrientTowardsEpicenter = false;
};
