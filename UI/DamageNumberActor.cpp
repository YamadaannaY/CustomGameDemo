#include "DamageNumberActor.h"

#include "DamageNumberWidget.h"
#include "Components/WidgetComponent.h"


ADamageNumberActor::ADamageNumberActor()
{
	PrimaryActorTick.bCanEverTick = true;

	WidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("DamageNumberWidget"));
	SetRootComponent(WidgetComponent);

	// Screen 空间：始终面向相机、按像素固定大小，省掉世界空间 Widget 的朝向处理
	WidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	WidgetComponent->SetDrawAtDesiredSize(true);
	WidgetComponent->SetTwoSided(true);
	WidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComponent->SetGenerateOverlapEvents(false);
}

void ADamageNumberActor::BeginPlay()
{
	Super::BeginPlay();

	StartLocation = GetActorLocation();
}

void ADamageNumberActor::InitDamageNumber(float Damage)
{
	PendingDamage = FMath::Max(0.f, Damage);
	
	//起始位置做一个随机偏移，尽量使每个数字升起的起始位置把不同，避免多次伤害数字重叠
	const float OffsetX = FMath::FRandRange(-HorizontalRandomOffset, HorizontalRandomOffset);
	SetActorLocation(GetActorLocation() + FVector(OffsetX, 0.f, 0.f));
	StartLocation = GetActorLocation();

	// SpawnActor 后 BeginPlay 可能已经跑过
	ApplyDamageToWidget();
}

void ADamageNumberActor::ApplyDamageToWidget()
{
	if (!WidgetComponent)
	{
		return;
	}

	if (DamageNumberWidgetClass && WidgetComponent->GetWidgetClass() != DamageNumberWidgetClass)
	{
		WidgetComponent->SetWidgetClass(DamageNumberWidgetClass);
		WidgetComponent->InitWidget();
	}

	if (UDamageNumberWidget* DamageWidget = Cast<UDamageNumberWidget>(WidgetComponent->GetUserWidgetObject()))
	{
		DamageWidget->ShowDamage(FMath::RoundToInt(PendingDamage));
	}
}

void ADamageNumberActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedTime += DeltaSeconds;
	const float LifeRatio = ElapsedTime / LifeTime;

	SetActorLocation(StartLocation + FVector(0.f, 0.f, RiseSpeed * ElapsedTime));

	if (UUserWidget* DamageWidget = WidgetComponent ? WidgetComponent->GetUserWidgetObject() : nullptr)
	{
		//比例值判断，转换为Render透明值
		const float FadeRatio = FMath::Clamp(
			(LifeRatio - FadeStartRatio) / FMath::Max(1.f - FadeStartRatio, KINDA_SMALL_NUMBER), 0.f, 1.f);
		DamageWidget->SetRenderOpacity(1.f - FadeRatio);
	}

	if (LifeRatio >= 1.f)
	{
		Destroy();
	}
}
