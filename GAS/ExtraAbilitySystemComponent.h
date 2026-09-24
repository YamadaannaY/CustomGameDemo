// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "ExtraAbilitySystemComponent.generated.h"

class UExtraGameplayAbility;
class UGameplayEffect;
class UExtraGameAttributeSet;
struct FActiveGameplayEffectHandle;

UCLASS(ClassGroup = (GAS), meta = (BlueprintSpawnableComponent))
class EXTRACTGAMECHARACTER_API UExtraAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	/**	GAS在服务端做的初始化操作
	 *	- 初始化属性集 
	 *	- 应用初始 GE 
	 *	- 授予角色级GA（天生拥有）
	 **/ 
	void ServerSideInit();

	// UnPossess 时清理天生 GA 和 GE
	void RemoveInnateAbilities();

	// ── Skill_02 充能信息的屏幕调试─────────────────────
	// 层数与回充倒计时直接打印在屏幕上：不走角色 Tick，由 cooldown tag 的变化驱动刷新，
	// 未回满时另用计时器走倒计时（回满即停，最后一条信息靠打印时长留存）。
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	bool bShowSkill02ChargeDebug = true;

	// ── CancelWindow 持有者登记 ──────────────────────────────────
	// 【后摇可打断窗口】开启期间，持有者把自己登记在这里；任何 GA 在 CommitAbility 时
	// 查询此句柄，命中且非自身即在激活GA前取消这个CancelGA
	// 同一时刻只可能有一个Cancel窗口（一个 Montage 在播），故只存单个句柄。
	FGameplayAbilitySpecHandle GetCancelWindowHolder() const { return CancellingGASpecHandle; }
	void SetCancelWindowHolder(const FGameplayAbilitySpecHandle& InHandle) { CancellingGASpecHandle = InHandle; }

	// 仅当登记的是 InHandle 时才清除，避免旧 GA 的延迟清理误删新持有者
	void ClearCancelWindowHolder(const FGameplayAbilitySpecHandle& InHandle);

	// 角色级能力：全部以 INDEX_NONE 授予，触发方式由各 GA 自身的 AbilityTriggers（InputTag）决定。
	// eg: Dodge(GA_Evade)归这里，不可装卸，武器技能组(Combo/AirAttack等)归 WeaponData 的 GrantedAbilities，可装卸。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UExtraGameplayAbility>> InnateAbilities;

	// 初始化GE：ServerSideInit 时应用到自身
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	TArray<TSubclassOf<UGameplayEffect>> InitEffects;

	// 属性初始值 DataTable（每行一个 Actor 类对应的初始属性数值）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Innate")
	class UDataTable* AttributeDataTable;

private:

	// Skill_02 充能调试：挂 cooldown tag 变化回调并刷一次显示（仅本地玩家角色）
	void SetupSkill02ChargeDebug();

	// 按 Cooldown GE 的 stack 数与剩余时间刷新屏幕信息
	void RefreshSkill02ChargeDebug();

	// cooldown tag 计数变化：只管启停刷新计时器
	// （tag 计数在 GE 存在期间恒为 1，所以要靠计时器兜住 1→2 层这种计数不变的变化）
	void OnSkill02ChargeTagChanged(FGameplayTag Tag, int32 NewCount);

	FDelegateHandle Skill02ChargeTagEventHandle;
	FTimerHandle Skill02ChargeRefreshTimer;

	//将属性集添加到ASC中
	void InitializeBaseAttribute();

	//根据当前角色类型在 DataTable 中查找匹配行并赋值
	void InitializeAttributeFromDataTable(UExtraGameAttributeSet* AttrSet);
	
	//将GE应用到ASC中
	void ApplyInitialEffects();
	
	//将GA注册到ASC中
	void GiveInitialAbilities();

	TArray<FGameplayAbilitySpecHandle> InnateAbilityHandles;
	TArray<FActiveGameplayEffectHandle> InnateEffectHandles;

	// CancelWindow 持有者句柄；Invalid 表示当前无窗口
	FGameplayAbilitySpecHandle CancellingGASpecHandle;
};
