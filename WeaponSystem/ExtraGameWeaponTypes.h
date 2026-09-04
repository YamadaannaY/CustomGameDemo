// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ExtraGameWeaponTypes.generated.h"

class UExtraGameplayAbility;
class UGameplayEffect;

/**
 * 武器轨迹扫描配置（Sweep-based，用于伤害判定）
 * 攻击窗口内对 TraceSockets 逐帧做 prev→cur 球体扫描，帧间位移过大时细分。
 */
USTRUCT(BlueprintType)
struct EXTRACTGAMECHARACTER_API FExtraGameWeaponTraceConfig
{
	GENERATED_BODY()

	// 单个 Socket 的扫描球半径（近似剑刃有效宽度的一半）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	float TraceRadius = 4.f;

	// 帧间位移超过该值时将线段细分为多个子步，防止高速挥动穿模
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	float MaxStepDistance = 20.f;

	// 单帧最大子步数（位移极小时不细分）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	int32 MaxSubSteps = 4;
};

/**
 * 单个武器 Mesh 的视觉配置（纯视觉，不含 GAS 数据）
 * 一个武器组可包含多个 WeaponEntry，各自独立显隐。
 */
USTRUCT(BlueprintType)
struct EXTRACTGAMECHARACTER_API FExtraGameWeaponEntry
{
	GENERATED_BODY()

	// ── 标识 ────────────────────────────────────────────────
	// 武器唯一 Tag（如 "Weapon.Mesh.Sword"）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Identity")
	FGameplayTag WeaponTag;

	// 武器显示名称（UI 用）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Identity")
	FText WeaponDisplayName;

	// ── 视觉 ────────────────────────────────────────────────
	// 武器 Mesh（软引用，按需异步加载）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UStaticMesh> WeaponMesh;

	// 绑定到的角色骨骼 Socket 名称（如 "hand_r_weapon"）。
	// 作为默认挂点：生成时挂载于此；重挂接口/AN 未指定 socket 时复位回此。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName AttachSocketName;

	// 可选显示挂点（角色骨骼 Socket，不含默认 AttachSocketName）。
	// 动画期间可用 AN_WeaponVisibility / ShowWeaponEntryOnSocket 把本武器重挂到其中某个并显示；
	// 为空则本武器只有默认挂点。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TArray<FName> AltAttachSockets;

	// 相对 Socket 的偏移
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FTransform RelativeTransform = FTransform::Identity;

	// ── 轨迹扫描 ────────────────────────────────────────────────
	// 参与伤害扫描的武器 Socket（如 柄部/中段/剑尖，在 StaticMesh 资源中定义）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	TArray<FName> TraceSockets;

	// 该武器的轨迹扫描配置（球半径 / 子步细分）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	FExtraGameWeaponTraceConfig TraceConfig;

	// 辅助：SocketName 是否属于本武器可用的显示挂点（默认 AttachSocketName 或 AltAttachSockets 之一）
	bool IsDisplaySocket(const FName SocketName) const
	{
		return SocketName == AttachSocketName || AltAttachSockets.Contains(SocketName);
	}
};

/**
 * 武器组完整配置
 * 一个武器组 = 多个 Weapon Mesh + 一套共享的 GA / GE / Tags / Params。
 *
 * 使用方式：
 * 
 * - 切换武器组 = 旧组全部隐藏 + 新组全部显示 + GA/GE 整体替换
 */
USTRUCT(BlueprintType)
struct EXTRACTGAMECHARACTER_API FExtraGameWeaponGroup
{
	GENERATED_BODY()
	
	// 武器组显示名称（UI 用）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Identity")
	FText GroupDisplayName;

	// ── 视觉：该组包含的所有武器 Mesh ──────────────────────
	// 同一组内的多个 Entry 可各自独立显隐
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Visual")
	TArray<FExtraGameWeaponEntry> WeaponEntries;

	// ── GAS ─────────────────────────────────────────────────
	// 装备该武器组时授予的 GameplayAbility 列表
	// 索引 0-3 依次对应 LightAttack / HeavyAttack / Skill / Ultimate
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Abilities")
	TArray<TSubclassOf<UExtraGameplayAbility>> GrantedAbilities;

	// 装备该武器组时持续应用的 GameplayEffect（如属性修正）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Effects")
	TArray<TSubclassOf<UGameplayEffect>> GrantedEffects;

	// ── 扩展 ────────────────────────────────────────────────
	// 装备该武器组时添加到 ASC 的额外 GameplayTag
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Tags")
	FGameplayTagContainer AdditionalTags;

	// 通用数值参数（伤害倍率、范围等）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Params")
	TMap<FName, float> NumericParams;
	
	UPROPERTY(EditDefaultsOnly,Category = "Group|Params")
	TSet<FGameplayTag> WeaponShouldNotCauseDamage ; 
	
	// 辅助：获取数值参数（带默认值）
	float GetNumericParam(FName ParamName, float DefaultValue = 0.f) const
	{
		if (const float* Found = NumericParams.Find(ParamName))
		{
			return *Found;
		}
		return DefaultValue;
	}

	// 辅助：按 WeaponTag 查找组内的 Entry
	const FExtraGameWeaponEntry* FindWeaponEntry(FGameplayTag WeaponTag) const
	{
		for (const FExtraGameWeaponEntry& Entry : WeaponEntries)
		{
			if (Entry.WeaponTag == WeaponTag)
			{
				return &Entry;
			}
		}
		return nullptr;
	}
};
