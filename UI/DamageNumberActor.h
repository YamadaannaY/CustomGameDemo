#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DamageNumberActor.generated.h"

class UWidgetComponent;
class UDamageNumberWidget;

/**
 * 单个伤害飘字：Screen 空间 WidgetComponent 跟随世界坐标上飘并淡出，结束自销毁。
 * 生成与数值由 GameplayCue 负责，本类只做表现。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API ADamageNumberActor : public AActor
{
	GENERATED_BODY()

public:
	ADamageNumberActor();

	// 生成后立即调用：传入本次伤害值
	void InitDamageNumber(float Damage);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DamageNumber")
	TObjectPtr<UWidgetComponent> WidgetComponent;

	// 飘字 WBP（父类 UDamageNumberWidget），蓝图子类里配置
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DamageNumber")
	TSubclassOf<UDamageNumberWidget> DamageNumberWidgetClass;

	// 存活时长（秒）
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber", meta = (ClampMin = "0.1"))
	float LifeTime = 0.9f;

	// 上飘速度（cm/s）
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber")
	float RiseSpeed = 70.f;

	// 生命周期走到多少比例开始淡出（0.5 = 后半程淡出）
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber", meta = (ClampMin = "0", ClampMax = "0.95"))
	float FadeStartRatio = 0.5f;

	// 生成时的水平随机偏移（cm），避免同帧多个数字完全重叠
	UPROPERTY(EditDefaultsOnly, Category = "DamageNumber")
	float HorizontalRandomOffset = 25.f;

private:
	void ApplyDamageToWidget();

	float PendingDamage = 0.f;
	float ElapsedTime = 0.f;
	FVector StartLocation = FVector::ZeroVector;
};
