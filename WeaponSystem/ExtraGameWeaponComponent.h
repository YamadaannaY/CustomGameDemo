// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Engine/EngineTypes.h"
#include "ExtraGameWeaponTypes.h"
#include "ExtraGameWeaponComponent.generated.h"

class UExtraGameWeaponData;
class UAbilitySystemComponent;
class UExtraGameplayAbility;
class UGameplayEffect;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AActor;
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
 * – 处理攻击窗口下武器轨迹插值扫描
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
	
	// 当前装备的武器组 Tag
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Runtime")
	FGameplayTag CurrentGroupTag;

	// 当前武器组 Mesh 整体是否可见（过场 / 攀爬时设为 false）
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Runtime")
	bool bWeaponVisible = true;

	// ── 显隐 Fade 配置（材质 FadeAmount 驱动） ──────────────
	// 武器材质中控制透明度的标量参数名（-1 = 不透明，1 = 完全透明）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fade")
	FName FadeParameterName = TEXT("FadeAmount");

	// 淡入 / 淡出时长（秒）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fade")
	float WeaponFadeDuration = 0.3f;

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
	
	/** 显示当前武器组所有 Mesh，不包括手动隐藏的对象，用于特定情况后恢复原本显示配置（如果要显示所有武器，调用ShowAllWeapons）*/
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void ShowWeapon();

	/** 强制显示当前武器组所有 Mesh，同时清空逐条目隐藏状态 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Visibility")
	void ShowAllWeapons();

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

	const FExtraGameWeaponGroup* GetCurrentWeaponGroup() const;

	/** 通过 GroupTag 获取武器组数据 */
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
	FOnWeaponGroupChanged OnWeaponGroupChanged;
	
	UFUNCTION()
	void WeaponGroupChangeDebug(FGameplayTag OldGroupTag , FGameplayTag NewGroupTag);

	// 在 ASC 初始化后调用（由 Character::ServerSideInit 触发）
	void OnASCInitialized();

	// ── 轨迹伤害扫描（由 ANS_WeaponTrace 驱动） ─────────────────
	// 攻击窗口开始：收集当前武器组参与扫描的 Socket 并清空窗口状态
	void BeginWeaponTrace();

	// 每动画帧推进：记录各 Socket 世界坐标，对 prev→cur 做球体扫描
	void TickWeaponTrace();

	// 攻击窗口结束：把本窗口命中集合封装为 TargetData 事件发出
	void EndWeaponTrace();

	// 是否处于攻击扫描窗口内
	bool IsTraceActive() const { return bTraceActive; }

	// ── 轨迹扫描配置 ───────────────────────────────────────────
	// 武器扫描使用的碰撞通道（需在项目设置 Collision 中配置其响应）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	TEnumAsByte<ECollisionChannel> WeaponTraceChannel = ECC_GameTraceChannel1;

	// 命中事件 Tag（窗口结束时发送给 Owner；默认走连段伤害事件，见构造函数）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace")
	FGameplayTag TraceEventTag;

	// 调试：绘制扫描线段与球体
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Trace|Debug")
	bool bDrawWeaponTraceDebug = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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

	// ── 显隐 Fade（材质 FadeAmount 驱动） ────────────────────
	// 单武器 Fade 运行时状态（非反射，无需序列化）
	struct FExtraWeaponFadeState
	{
		float CurrentFade = -1.f;  // 当前材质参数值（-1 不透明 → 1 全透明）
		float TargetFade = -1.f;   // 目标值
	};

	// 每武器 Fade 状态（淡出完成后保留为 {1,1}，供重复 Show/Hide 判断已到位）
	TMap<FGameplayTag, FExtraWeaponFadeState> WeaponFadeStates;

	// 每武器缓存的全部 Material Slot 动态实例（在 SpawnWeaponMesh 时创建）
	TMap<FGameplayTag, TArray<TObjectPtr<UMaterialInstanceDynamic>>> WeaponDynamicMIs;

	// 请求渐变淡入/淡出（用户级显隐入口调用；bFadeIn=true 淡入到不透明）
	void RequestWeaponFade(FGameplayTag WeaponTag, bool bFadeIn);

	// 瞬时设置 Fade 到位（生成 / 装备 / 切换时用，无动画）
	void InitWeaponFade(FGameplayTag WeaponTag, bool bVisible);

	// 把 Fade 值写入该武器全部缓存的 DMI
	void SetWeaponFadeValue(FGameplayTag WeaponTag, float Value);

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

	// ── 轨迹扫描状态 ────────────────────────────────────────
	bool bTraceActive = false;
	// 本窗口参与扫描的 Socket 名称
	TArray<FName> ActiveTraceSockets;
	// 本窗口使用的扫描配置（取当前组内主武器的 TraceConfig）
	FExtraGameWeaponTraceConfig ActiveTraceConfig;
	// Socket 名称 → 负责该 Socket 的武器 Mesh 组件
	TMap<FName, TObjectPtr<UStaticMeshComponent>> ActiveTraceSocketToMesh;
	// Socket 名称 → 上一帧世界坐标（首帧不扫描）
	TMap<FName, FVector> TraceSocketPrevLocations;
	// 已命中黑名单：本窗口内已结算过的 Actor，重复扫到不再结算；窗口结束清空
	TSet<TWeakObjectPtr<AActor>> AlreadyHitActors;

	// 从当前武器组收集参与扫描的 Socket 与配置
	bool GatherTraceSocketsFromCurrentGroup();
	// 对单个 Socket 做 prev→cur 球体扫描（子步细分 + 去重）
	void TraceSocketSegment(const FName SocketName, const FVector& Prev, const FVector& Curr);
	// 命中即时结算：把单个 Actor 封装为 TargetData 事件发出（GA 收到后当帧应用 GE）
	void SendHitEventForActor(AActor* HitActor);
};
