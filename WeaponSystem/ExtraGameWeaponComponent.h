// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "ExtraGameWeaponTypes.h"
#include "ExtraGameWeaponComponent.generated.h"

class UExtraGameWeaponData;
class UAbilitySystemComponent;
class UExtraGameplayAbility;
class UGameplayEffect;
class UStaticMeshComponent;
struct FExtraGameWeaponGroup;

// 武器组变化委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponGroupChanged, FGameplayTag, OldGroupTag, FGameplayTag, NewGroupTag);

/**
 * 武器管理组件
 * 附加在 AExtraCharacter 上，管理该角色拥有的全部武器组。
 *
 * 核心职责：
 * - 武器组切换：旧组全部隐藏 + 移除 GA/GE → 新组全部显示 + 授予 GA/GE
 * - 逐武器显隐：组内的单个 Weapon Mesh 可独立 Show/Hide
 * - GAS 管理：GA/GE/Tags 以武器组为单位进行授予和移除
 */
UCLASS(ClassGroup=(Weapon), meta=(BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraGameWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtraGameWeaponComponent();

	// ── 配置 ─────────────────────────────────────────────────
	// 武器 DataAsset（在角色蓝图中配置）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	TObjectPtr<UExtraGameWeaponData> WeaponDataAsset;

	// ── 运行时状态 ──────────────────────────────────────────
	// 当前装备的武器组 Tag
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Runtime")
	FGameplayTag CurrentGroupTag;

	// 当前武器组 Mesh 整体是否可见（过场 / 攀爬时设为 false）
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Runtime")
	bool bWeaponVisible = true;

	// ── 武器组级操作 ────────────────────────────────────────

	/** 装备武器组（生成 Mesh → 应用 GE → 授予 GA） */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Group")
	bool EquipWeaponGroup(FGameplayTag GroupTag);

	/** 卸载当前武器组（移除 GA → 移除 GE → 隐藏 Mesh） */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Group")
	void UnequipWeaponGroup();

	/** 切换到另一个武器组 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Group")
	bool SwitchWeaponGroup(FGameplayTag NewGroupTag);

	/** 循环切换到下一个武器组（按 DataAsset 中的顺序） */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Group")
	bool CycleToNextWeaponGroup();

	// ── 整体显隐（所有 Mesh） ───────────────────────────────

	/** 显示当前武器组所有 Mesh（过场后恢复显示等） */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void ShowWeapon();

	/** 隐藏当前武器组所有 Mesh（过场、攀爬、游泳等） */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void HideWeapon();

	// ── 逐武器显隐（组内单个 Mesh） ─────────────────────────

	/** 显示当前武器组中的指定武器 Mesh */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void ShowWeaponEntry(FGameplayTag WeaponTag);

	/** 隐藏当前武器组中的指定武器 Mesh */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void HideWeaponEntry(FGameplayTag WeaponTag);

	/** 查询指定武器 Mesh 是否可见 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Visibility")
	bool IsWeaponEntryVisible(FGameplayTag WeaponTag) const;

	// ── 查询 API ────────────────────────────────────────────

	/** 获取当前武器组完整数据（C++ 专用，可能返回 nullptr） */
	const FExtraGameWeaponGroup* GetCurrentWeaponGroup() const;

	/** 通过 GroupTag 获取武器组数据（C++ 专用，可能返回 nullptr） */
	const FExtraGameWeaponGroup* GetWeaponGroupByTag(FGameplayTag GroupTag) const;

	/** 蓝图：获取当前武器组数据，返回是否有效 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Query")
	bool GetCurrentWeaponGroupBP(FExtraGameWeaponGroup& OutGroup) const;

	/** 蓝图：通过 GroupTag 获取武器组数据，返回是否找到 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Query")
	bool GetWeaponGroupByTagBP(FGameplayTag GroupTag, FExtraGameWeaponGroup& OutGroup) const;

	/** 通过 WeaponTag 获取对应的 Mesh 组件 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Query")
	UStaticMeshComponent* GetWeaponMeshByTag(FGameplayTag WeaponTag) const;

	/** 获取当前武器组中所有 Mesh 组件 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Query")
	TArray<UStaticMeshComponent*> GetCurrentGroupMeshes() const;

	// ── 事件 ─────────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponGroupChanged OnWeaponGroupChanged;

	// 在 ASC 初始化后调用（由 Character::ServerSideInit 触发）
	void OnASCInitialized();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// ── Mesh 管理 ────────────────────────────────────────────
	// 已生成的全部武器 Mesh（WeaponTag → MeshComponent），切换组时仅改可见性
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UStaticMeshComponent>> SpawnedWeaponMeshes;

	// 逐条目隐藏标记：在此集合中的 WeaponTag 被手动隐藏
	// ShowWeaponEntry 从中移除，HideWeaponEntry 添加到此集合
	UPROPERTY()
	TSet<FGameplayTag> HiddenWeaponEntries;

	// 生成武器 Mesh
	UStaticMeshComponent* SpawnWeaponMesh(const FExtraGameWeaponEntry& Entry);

	// 销毁武器 Mesh
	void DestroyWeaponMesh(FGameplayTag WeaponTag);

	// 设置指定武器的可见性（同时检查 bWeaponVisible 和 HiddenWeaponEntries）
	void SetWeaponMeshVisibility(FGameplayTag WeaponTag, bool bVisible);

	// 隐藏所有已生成的武器 Mesh
	void HideAllWeaponMeshes();

	// 隐藏指定武器组中所有 Mesh
	void HideGroupWeaponMeshes(FGameplayTag GroupTag);

	// ── GAS 状态管理 ─────────────────────────────────────────
	// 缓存的 ASC 引用
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> OwnerASC;

	// 当前武器组授予的 GA Handle（Unequip 时移除）
	TArray<FGameplayAbilitySpecHandle> ActiveAbilityHandles;

	// 当前武器组应用的 GE Handle（Unequip 时移除）
	TArray<FActiveGameplayEffectHandle> ActiveEffectHandles;

	// 移除所有活跃的 GA
	void RemoveGrantedAbilities();

	// 移除所有活跃的 GE
	void RemoveGrantedEffects();

	// 授予指定武器组的 GA
	void GrantWeaponGroupAbilities(const FExtraGameWeaponGroup& Group);

	// 应用指定武器组的持续 GE
	void ApplyWeaponGroupEffects(const FExtraGameWeaponGroup& Group);

	// 更新 ASC 上的 Loose GameplayTags
	void UpdateCharacterTags(const FExtraGameWeaponGroup* OldGroup, const FExtraGameWeaponGroup* NewGroup);

	// ── 内部辅助 ────────────────────────────────────────────
	void CacheOwnerASC();
};
