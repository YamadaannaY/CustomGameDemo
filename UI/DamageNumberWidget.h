#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageNumberWidget.generated.h"

/**
 * 伤害飘字 Widget 基类：C++ 端只负责传入数值，
 * 文本格式、字体、描边、颜色与动画全部交给蓝图配置值实现。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//蓝图里 SetText 并播放弹出动画
	UFUNCTION(BlueprintImplementableEvent, Category = "DamageNumber")
	void ShowDamage(int32 Damage);
};
