#include "ExtraCharacter.h"
#include "ExtractGameCharacter.h"
#include "ExtraGameMovementComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "WeaponSystem/ExtraGameWeaponComponent.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "UI/OverHeadStatsGauge.h"


AExtraCharacter::AExtraCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtraGameMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);

	// 允许被武器轨迹扫描（SweepMultiByChannel ECC_GameTraceChannel1）命中；
	// 玩家自身由扫描代码 AddIgnoredActor(Owner) 免疫，队友命中需后续按 Team 过滤。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);

	// 相机臂专用通道不遮挡：紧贴敌人时臂若被胶囊挡住，臂长会被压到命中点、相机怼到角色身前
	// （下砸等前置视角镜头尤其明显）。墙地仍照常挡——该通道默认响应为 Block。
	// 玩家与 AI 都继承本类，改这一处即可覆盖全部角色。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_SpringArm, ECR_Ignore);

	// ── GAS ──
	AbilitySystemComponent = CreateDefaultSubobject<UExtraAbilitySystemComponent>(TEXT("ASC"));

	// ── 武器组件 ──
	WeaponComponent = CreateDefaultSubobject<UExtraGameWeaponComponent>(TEXT("WeaponComponent"));

	// ── 头顶血条 ──
	OverHeadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverHeadWidget"));
	OverHeadWidgetComponent->SetupAttachment(GetMesh());
	OverHeadWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	OverHeadWidgetComponent->SetDrawAtDesiredSize(false);
	// 世界模式默认单面材质，背对相机时会整个消失
	OverHeadWidgetComponent->SetTwoSided(true);
	// 旋转与父级 Mesh 解绑：否则世界朝向 = Mesh 朝向 × 设定值，角色转身血条跟着转
	OverHeadWidgetComponent->SetUsingAbsoluteRotation(true);
	OverHeadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 190.f));
	OverHeadWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverHeadWidgetComponent->SetHiddenInGame(true);
}

void AExtraCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!OverHeadWidgetComponent) return;

	// 世界模式下 DrawSize 是世界尺寸(cm)：300x30 就是 3 米宽；它同时是 UMG 画布与渲染分辨率，
	// 直接改小会压垮 Widget 内部布局（那里按 300x30 像素排），所以世界尺寸靠缩放收。
	OverHeadWidgetComponent->SetDrawSize(FVector2D(OverHeadGaugeXSize, OverHeadGaugeYSize));
	OverHeadWidgetComponent->SetRelativeScale3D(FVector(OverHeadGaugeWorldScale));
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
		UpdateHeadGaugeRotation();

		//每次配置重置UpdateTime
		GetWorldTimerManager().ClearTimer(HeadStatGaugeVisibilityUpdateTimerHandle);
		GetWorldTimerManager().ClearTimer(HeadStatGaugeRotTimerHandle);
		GetWorldTimerManager().SetTimer(HeadStatGaugeVisibilityUpdateTimerHandle,this,&AExtraCharacter::UpdateHeadGaugeVisibility,HeadStatGaugeVisibilityUpdateGap,true);
		GetWorldTimerManager().SetTimer(HeadStatGaugeRotTimerHandle,this,&AExtraCharacter::UpdateHeadGaugeRotation,HeadStatGaugeRotationUpdateGap,true);
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

void AExtraCharacter::UpdateHeadGaugeRotation() const
{
	if (!OverHeadWidgetComponent) return;

	//血条挂在别人身上，本地相机只能现取：缓存到本Actor成员上的话，非本地角色永远拿不到值
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!CameraManager) return;

	const FVector GaugeLocation = OverHeadWidgetComponent->GetComponentLocation();
	FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(GaugeLocation, CameraManager->GetCameraLocation());

	//只清 Roll（FindLookAtRotation 出来的 Roll 本就≈0）；Pitch 保留，否则血条只是水平朝向相机，俯视时会被严重压扁
	LookAtRot.Roll = 0.f;

	//组件是绝对旋转（构造里 SetUsingAbsoluteRotation），这里设的就是世界朝向而非Relative
	OverHeadWidgetComponent->SetWorldRotation(LookAtRot);
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
