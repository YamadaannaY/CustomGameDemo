#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtraAICharacter.generated.h"

/**
 * AI 怪物基类（当前承担训练木桩职责）
 *
 * 复用 AExtraCharacter 的 GAS(ASC + AttributeSet) / Team 分配 / 头顶血条：
 * - 与玩家唯一差异是「无 Controller Possess」，因此在 BeginPlay 里主动 ServerSideInit()。
 * - 胶囊显式 Block 武器扫描通道，使玩家攻击的轨迹扫描能命中本 Actor。
 * 后续真实怪物（会移动/攻击）直接继承本类即可。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraAICharacter : public AExtraCharacter
{
	GENERATED_BODY()

public:
	AExtraAICharacter(const FObjectInitializer& ObjectInitializer);

	// 无 Controller，主动初始化 GAS；跳过武器组件装备（木桩不配 WeaponDataAsset）
	virtual void ServerSideInit() override;

protected:
	virtual void BeginPlay() override;

	// 武器扫描通道：胶囊需 Block 该通道才会被 SweepMultiByChannel 命中
	// （与武器组件 WeaponTraceChannel 默认值一致，可在 BP 中联动配置）
	UPROPERTY(EditDefaultsOnly, Category = "AI|Collision")
	TEnumAsByte<ECollisionChannel> WeaponTraceChannel = ECC_GameTraceChannel1;
};
