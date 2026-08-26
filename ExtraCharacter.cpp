#include "ExtraCharacter.h"
#include "ExtraGameMovementComponent.h"
#include "Components/WidgetComponent.h"
#include "WeaponSystem/ExtraGameWeaponComponent.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UI/OverHeadStatsGauge.h"


AExtraCharacter::AExtraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtraGameMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);

	// ── GAS ──
	AbilitySystemComponent = CreateDefaultSubobject<UExtraAbilitySystemComponent>(TEXT("ASC"));

	// ── 武器组件 ──
	WeaponComponent = CreateDefaultSubobject<UExtraGameWeaponComponent>(TEXT("WeaponComponent"));

	// ── 头顶血条 ──
	OverHeadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverHeadWidget"));
	OverHeadWidgetComponent->SetupAttachment(GetMesh());
	OverHeadWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	OverHeadWidgetComponent->SetDrawSize(FVector2D(120.f, 24.f));
	OverHeadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 190.f));
	OverHeadWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverHeadWidgetComponent->SetHiddenInGame(true);
}

UAbilitySystemComponent* AExtraCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AExtraCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AExtraCharacter,TeamID);
}

void AExtraCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	TeamID=NewTeamID;
}

FGenericTeamId AExtraCharacter::GetGenericTeamId() const
{
	return TeamID;
}

void AExtraCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 服务端给每个角色分配唯一团队 ID，使所有角色互为敌对（任意两个角色 TeamID 都不同）
	if (HasAuthority())
	{
		static uint8 NextTeamID = 0;
		SetGenericTeamId(FGenericTeamId(NextTeamID++));
	}

	// 把目标跳跃高度换算成起跳初速度：v = sqrt(2 * g * h)（g 取绝对值，单位均为 cm）
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const float Gravity = FMath::Abs(Movement->GetGravityZ());
		Movement->JumpZVelocity = FMath::Sqrt(2.f * Gravity * JumpHeight);
	}

	// 客户端为非本地玩家角色配置头顶血条（本地玩家自己的角色不显示）
	if (GetNetMode() != NM_DedicatedServer && !IsLocallyControlledByPlayer())
	{
		ConfigureOverHeadStatusWidget();
	}
}

void AExtraCharacter::ConfigureOverHeadStatusWidget()
{
	if (!OverHeadWidgetComponent) return ;

	//本地客户端下Character不需要OverheadUI
	if (IsLocallyControlledByPlayer())
	{
		OverHeadWidgetComponent->SetHiddenInGame(true);
		return;
	}

	// 确保 WidgetClass 已设置（优先使用 C++ 配置项，否则保留 WidgetComponent 上蓝图配置的类）
	if (OverHeadWidgetClass && OverHeadWidgetComponent->GetWidgetClass() != OverHeadWidgetClass)
	{
		OverHeadWidgetComponent->SetWidgetClass(OverHeadWidgetClass);
	}

	//非LocalPlayer
	UOverHeadStatsGauge* OverHeadStatsGauge=Cast<UOverHeadStatsGauge>(OverHeadWidgetComponent->GetUserWidgetObject());
	if (OverHeadStatsGauge)
	{
		//监听Health/Mana
		OverHeadStatsGauge->ConfigureWithASC(GetAbilitySystemComponent());
		OverHeadStatsGauge->SetBarColorsByTeam(GetGenericTeamId());
		OverHeadWidgetComponent->SetHiddenInGame(false);

		UpdateHeadGaugeVisibility();

		//每次配置重置UpdateTime
		GetWorldTimerManager().ClearTimer(HeadStatGaugeVisibilityUpdateTimerHandle);
		GetWorldTimerManager().SetTimer(HeadStatGaugeVisibilityUpdateTimerHandle,this,&AExtraCharacter::UpdateHeadGaugeVisibility,HeadStatGaugeVisibilityUpdateGap,true);
	}
}

void AExtraCharacter::UpdateHeadGaugeVisibility() const
{
	APawn* LocalPlayerPawn=UGameplayStatics::GetPlayerPawn(this,0);
	
	if(LocalPlayerPawn)
	{
		//当前Character与本地Pawn的距离差值平方
		float DistSquared=FVector::DistSquared(GetActorLocation(),LocalPlayerPawn->GetActorLocation());

		//决定是否显示UI
		OverHeadWidgetComponent->SetHiddenInGame(DistSquared>HeadStatGaugeVisibilityRangeSquared);
	}
}

void AExtraCharacter::SetStatusGaugeEnabled(bool bEnabled)
{
	if (bEnabled)
	{
		ConfigureOverHeadStatusWidget();
		OverHeadWidgetComponent->SetVisibility(true);
	}
	else
	{
		OverHeadWidgetComponent->SetHiddenInGame(true);
		OverHeadWidgetComponent->SetVisibility(false);
	}
}

void AExtraCharacter::OnRep_TeamID()
{
	// 客户端收到 TeamID 复制后刷新头顶血条颜色（BeginPlay 时 TeamID 可能尚未复制到位）
	if (OverHeadWidgetComponent && !IsLocallyControlledByPlayer())
	{
		if (UOverHeadStatsGauge* Gauge = Cast<UOverHeadStatsGauge>(OverHeadWidgetComponent->GetUserWidgetObject()))
		{
			Gauge->SetBarColorsByTeam(GetGenericTeamId());
		}
	}
}

void AExtraCharacter::ServerSideInit()
{
	if (bGASInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ExtraCharacter] ServerSideInit: GAS already initialized. Skipping duplicate call."));
		return;
	}

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		AbilitySystemComponent->ServerSideInit();
	}

	// ASC 初始化完毕后，通知武器组件装备默认武器组
	if (WeaponComponent)
	{
		WeaponComponent->OnASCInitialized();
	}

	bGASInitialized = true;
}
