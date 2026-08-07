// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ExtraGameWeaponTypes.generated.h"

class UExtraGameplayAbility;
class UGameplayEffect;

/**
 * 武器技能输入 ID
 * 每个武器 GA 通过 AbilityTriggers 绑定对应的 InputID，
 * ASC 收到输入后自动路由到当前已授予的 GA。
 */
UENUM(BlueprintType)
enum class EWeaponAbilityInputID : uint8
{
	None,
	Confirm,      // 0
	Cancel,       // 1
	LightAttack,  // 2
	HeavyAttack,  // 3
	Skill,        // 4
	Ultimate,     // 5
	Dodge         // 6
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

	// 绑定到的角色骨骼 Socket 名称（如 "hand_r_weapon"）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName AttachSocketName;

	// 相对 Socket 的偏移
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FTransform RelativeTransform = FTransform::Identity;
};

/**
 * 武器组完整配置
 * 一个武器组 = 多个 Weapon Mesh + 一套共享的 GA / GE / Tags / Params。
 *
 * 使用方式：
 * - 双持：WeaponEntries 放入 [左手刀, 右手刀]，共享同一套技能 GA
 * - 剑盾：WeaponEntries 放入 [剑, 盾牌]，技能 GA 同时作用于两者
 * - 切换武器组 = 旧组全部隐藏 + 新组全部显示 + GA/GE 整体替换
 */
USTRUCT(BlueprintType)
struct EXTRACTGAMECHARACTER_API FExtraGameWeaponGroup
{
	GENERATED_BODY()

	// ── 标识 ────────────────────────────────────────────────
	// 武器组唯一 Tag（如 "WeaponGroup.DualSwords"）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Group|Identity")
	FGameplayTag GroupTag;

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
