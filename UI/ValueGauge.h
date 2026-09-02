// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ValueGauge.generated.h"

class UAbilitySystemComponent;
struct FGameplayAttribute;
struct FOnAttributeChangeData;

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UValueGauge : public UUserWidget
{
	GENERATED_BODY()
	
public:
	//在UI生成之前就设置好一些变量参数
	virtual void NativePreConstruct() override;

	//在指定ASC中找到两个Value，绑定属性值变化委托回调函数，根据值变化更新Percent
	void SetAndBoundToGameplayAttribute(UAbilitySystemComponent* AbilitySystemComponent,const FGameplayAttribute& Attribute,const FGameplayAttribute& MaxAttribute);

	//绑定 Shield 属性并驱动金色护盾覆盖条（填充方向 RightToLeft，始终在血条右端）
	void SetAndBoundToShieldAttribute(UAbilitySystemComponent* AbilitySystemComponent);

	//属性值变化后回调调用，计算并更新Percent，更新Text
	void SetValue(float NewValue,float NewMaxValue);

	//运行时动态修改进度条填充颜色
	void SetFillColor(FLinearColor NewColor);

	//设置护盾进度条填充颜色
	void SetShieldFillColor(FLinearColor NewColor);
private:
	//SetAndBoundToGameplayAttribute中两个值绑定的回调
	void ValueChanged(const FOnAttributeChangeData& ChangeData);
	void MaxValueChanged(const FOnAttributeChangeData& ChangeData);

	//Shield属性值变化的回调
	void ShieldChanged(const FOnAttributeChangeData& ChangeData);

	// 初始占位：CacheValue=0 防未初始化；CacheMaxValue=-1 表示「从未就绪」，
	// 用于区分「绑定早于属性复制（Max 短暂为 0，正常时序）」与「有效 Max 异常归零（应报警）」
	float CacheValue = 0.f;
	float CacheMaxValue = -1.f;

	// 护盾条缓存值（用于当 MaxHealth 变化时与 ShieldValueChanged 配合刷新）
	float CacheShieldValue = 0.f;

	UPROPERTY(EditAnywhere,Category="Visual")
	FLinearColor BarColor = FLinearColor::White;

	UPROPERTY(EditAnywhere,Category="Visual")
	FSlateFontInfo ValueTextFont;

	UPROPERTY(EditAnywhere,Category="Visual")
	bool bValueTextVisible=true;

	UPROPERTY(EditAnywhere,Category="Visual")
	bool bProgressBarVisible=true;

	UPROPERTY(VisibleAnywhere,meta=(BindWidget))
	class UProgressBar* ProgressBar;

	UPROPERTY(VisibleAnywhere,meta=(BindWidget))
	class UTextBlock* ValueText;

	// 护盾覆盖条（金色，RightToLeft 填充，与 ProgressBar 同位置叠加）
	UPROPERTY(VisibleAnywhere, meta=(BindWidget))
	UProgressBar* ShieldBar;
};
