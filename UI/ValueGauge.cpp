#include "ValueGauge.h"
#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"

void UValueGauge::NativePreConstruct()
{
	Super::NativePreConstruct();
	
	ValueText->SetVisibility(bValueTextVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);

	ProgressBar->SetVisibility(bProgressBarVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);

	// 应用 BarColor（SetFillColor 内部白化Style的绿色Tint，使编辑器里设置的颜色真正生效）
	SetFillColor(BarColor);

	// ShieldBar 默认隐藏，等绑定后根据 Shield 值决定是否显示
	// 设置为从右向左填充（RightToLeft），使金色条从血条右端向左增长，
	// 从而与 LeftToRight 的绿色 HealthBar 形成互补：重叠部分显示金色，未重叠部分各自独立
	if (ShieldBar)
	{
		ShieldBar->SetBarFillType(EProgressBarFillType::RightToLeft);
		ShieldBar->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UValueGauge::SetAndBoundToGameplayAttribute(UAbilitySystemComponent* AbilitySystemComponent,
	const FGameplayAttribute& Attribute, const FGameplayAttribute& MaxAttribute)
{
	if (AbilitySystemComponent)
	{
		bool bFound;

		//在指定ASC中寻找两个Value，如果找到进行一次初始化，随后正式开始追踪UI变化
		const  float Value=AbilitySystemComponent->GetGameplayAttributeValue(Attribute,bFound);
		const float MaxValue=AbilitySystemComponent->GetGameplayAttributeValue(MaxAttribute,bFound);

		if(bFound)
		{

			SetValue(Value,MaxValue);
		}

		//两个属性的值改变时回调的函数
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this,&ThisClass::ValueChanged);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(MaxAttribute).AddUObject(this,&ThisClass::MaxValueChanged);
	}
}

void UValueGauge::SetAndBoundToShieldAttribute(UAbilitySystemComponent* AbilitySystemComponent)
{
	if (!AbilitySystemComponent || !ShieldBar) return;

	// 初始化
	bool bFound = false;
	const float ShieldVal = AbilitySystemComponent->GetGameplayAttributeValue(
		UExtraGameAttributeSet::GetShieldAttribute(), bFound);
	if (bFound)
	{
		CacheShieldValue = ShieldVal;
		const float MaxHealth = AbilitySystemComponent->GetGameplayAttributeValue(
			UExtraGameAttributeSet::GetMaxHealthAttribute(), bFound);
		const float ShieldPercent = (MaxHealth > 0.f) ? FMath::Clamp(ShieldVal / MaxHealth, 0.f, 1.f) : 0.f;
		ShieldBar->SetPercent(ShieldPercent);
		ShieldBar->SetVisibility(ShieldVal > 0.f ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// 监听 Shield 属性变化（MaxHealth 变化已在 SetAndBoundToGameplayAttribute 中绑定，
	// SetValue 中会同步刷新 ShieldBar percent）
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UExtraGameAttributeSet::GetShieldAttribute()).AddUObject(this, &ThisClass::ShieldChanged);
}

void UValueGauge::SetValue(float NewValue, float NewMaxValue)
{
	//时刻更新改变后的值，实际上是这个函数被调用时的参数
	CacheValue=NewValue;
	CacheMaxValue=NewMaxValue;

	//NewMaxValue作为分母不能为0
	if (NewMaxValue==0)
	{
		UE_LOG(LogTemp,Warning,TEXT("ValueGauge:%s,NewMaxValue can`t be 0"),*GetName());
		return;
	}

	const float NewPercent=NewValue/NewMaxValue;
	ProgressBar->SetPercent(NewPercent);

	// 同时刷新 ShieldBar——分母（MaxHealth）变化后需更新 Shield/MaxHealth
	if (ShieldBar)
	{
		const float ShieldPercent = FMath::Clamp(CacheShieldValue / NewMaxValue, 0.f, 1.f);
		ShieldBar->SetPercent(ShieldPercent);
		ShieldBar->SetVisibility(CacheShieldValue > 0.f
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	//ValueText的设置，设置没有小数点，并且规定了Text的格式，传入值
	const FNumberFormattingOptions FormatOps=FNumberFormattingOptions().SetMaximumFractionalDigits(0);
	const FText NewText = FText::Format(
		FTextFormat::FromString("{0}/{1}"),
		FText::AsNumber(NewValue, &FormatOps),
		FText::AsNumber(NewMaxValue, &FormatOps)
	);
	ValueText->SetText(NewText);
}

void UValueGauge::SetFillColor(FLinearColor NewColor)
{
	BarColor = NewColor;
	if (ProgressBar)
	{
		// 白化 Style 的 FillImage Tint：进度条默认 Tint 为绿色，
		// 会与 FillColorAndOpacity 相乘，导致自定义颜色被绿色滤镜压盖。
		ProgressBar->SetFillColorAndOpacity(FLinearColor::White);
		ProgressBar->SetFillColorAndOpacity(NewColor);
		ProgressBar->SynchronizeProperties();
	}
}

void UValueGauge::SetShieldFillColor(FLinearColor NewColor)
{
	if (ShieldBar)
	{
		ShieldBar->SetFillColorAndOpacity(FLinearColor::White);
		ShieldBar->SetFillColorAndOpacity(NewColor);
		ShieldBar->SynchronizeProperties();
	}
}

void UValueGauge::ValueChanged(const FOnAttributeChangeData& ChangeData)
{
	SetValue(ChangeData.NewValue,CacheMaxValue);
}

void UValueGauge::MaxValueChanged(const FOnAttributeChangeData& ChangeData)
{
	SetValue(CacheValue,ChangeData.NewValue);
}

void UValueGauge::ShieldChanged(const FOnAttributeChangeData& ChangeData)
{
	CacheShieldValue = ChangeData.NewValue;
	const float MaxHealth = CacheMaxValue;
	const float ShieldPercent = (MaxHealth > 0.f) ? FMath::Clamp(ChangeData.NewValue / MaxHealth, 0.f, 1.f) : 0.f;

	if (ShieldBar)
	{
		ShieldBar->SetPercent(ShieldPercent);
		ShieldBar->SetVisibility(ChangeData.NewValue > 0.f
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}
