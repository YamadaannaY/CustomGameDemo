// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ExtraGameWeaponTypes.h"
#include "ExtraGameWeaponData.generated.h"

/**
 * 角色武器配置 DataAsset
 * 一个角色原型对应一个 DataAsset，包含该角色可使用的全部武器组。
 *
 * 使用方式：
 * 在角色BP的 WeaponComponent 上引用此 DataAsset
 */
UCLASS(BlueprintType)
class EXTRACTGAMECHARACTER_API UExtraGameWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 武器组映射：GroupTag → 武器组数据
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WeaponData")
	TMap<FGameplayTag, FExtraGameWeaponGroup> WeaponGroups;

	// 角色生成时默认装备的武器组 Tag
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WeaponData")
	FGameplayTag DefaultWeaponGroupTag;

	// ── 查询 API ─────────────────────────────────────────

	/** 通过 GroupTag 查找武器组 */
	const FExtraGameWeaponGroup* FindGroup(FGameplayTag GroupTag) const
	{
		return WeaponGroups.Find(GroupTag);
	}

	/** 检查是否包含指定 GroupTag 的武器组 */
	bool HasGroup(FGameplayTag GroupTag) const
	{
		return WeaponGroups.Contains(GroupTag);
	}

	/** 获取武器组数量 */
	int32 GetGroupCount() const
	{
		return WeaponGroups.Num();
	}

	/** 获取所有 GroupTag（用于遍历切换） */
	TArray<FGameplayTag> GetAllGroupTags() const
	{
		TArray<FGameplayTag> Tags;
		WeaponGroups.GenerateKeyArray(Tags);
		return Tags;
	}

	/** 反向查找：给定一个 Weapon Tag，找到它所属的武器组（可能返回 nullptr） */
	const FExtraGameWeaponGroup* FindGroupContainingWeapon(FGameplayTag WeaponTag) const
	{
		for (auto& Pair : WeaponGroups)
		{
			for (const FExtraGameWeaponEntry& Entry : Pair.Value.WeaponEntries)
			{
				if (Entry.WeaponTag == WeaponTag)
				{
					return &Pair.Value;
				}
			}
		}
		return nullptr;
	}
	
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(FPrimaryAssetType(TEXT("WeaponData")), GetFName());
	}
};
