#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/EngineTypes.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "ExtraAbilityBase.h"
#include "ExtraGameplayAbility.generated.h"

class UAnimMontage;
class UCharacterMovementComponent;
class UGameplayEffect;
class UMotionWarpingComponent;
class URootMotionModifier_Warp;

/**
 * 主动触发GA基类
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameplayAbility : public UExtraAbilityBase
{
	GENERATED_BODY()

public:
	UExtraGameplayAbility();

	UAnimInstance* GetOwnerAnimInstance() const;

	// 本 GA 的输入 Tag：授予时由 ASC 打到 spec 的动态 tag 上，输入层用同一个 Tag 调
	// AbilityInputTagPressed/Released 就能找到它。留空 = 不由输入触发（被动 / 工具类 GA）。
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	FGameplayTag InputTag;

	FORCEINLINE FGameplayTag GetInputTag() const { return InputTag; }

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	virtual void PreActivate(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData = nullptr) override;

protected:
	//默认在所有GA结束时将所有Weapon统一再次进行ClearShow操作，角色常态不持有武器
	UPROPERTY(EditAnywhere,Category="Weapon | Visible")
	bool ClearWeaponShowOnAbilityEnd = true ;

	// 是否启用移动打断机制，开启后CancelWindow内触发移动输入可以取消GA，进而结束Montage并惯性化到状态机
	UPROPERTY(EditDefaultsOnly, Category = "Movement | Cancel")
	bool bEnableMovementCancel = false;
	
	//MW有限追踪的最大值
	UPROPERTY(EditDefaultsOnly,Category= "MoveMent | MotionWarp")
	float MotionWarpMaxMoveDist = 150.f  ; 

	// 是否启用重力缩放(EndAbility 时自动恢复为激活前的原始值。)
	UPROPERTY(EditDefaultsOnly, Category = "Gravity Scale")
	bool bEnableGravityScale = false;

	// 激活期间的角色重力系数：0 = 无重力（匀速下落），1 = 引擎默认重力（持续加速）。
	UPROPERTY(EditDefaultsOnly, Category = "Gravity Scale", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableGravityScale"))
	float AbilityGravityScale = 1.0f;

	// 如果开启移动Cancel，开始监听取消事件
	void SetupMovementCancel();

	// 命中移动打断的瞬间回调
	virtual void OnMovementCancelTriggered();

	// 取消事件 Tag（默认 "ability.cancel"，子类可覆写）
	virtual FGameplayTag GetMovementCancelTag() const;

	// 取消事件回调。事件由 AN_CancelWindow 在区间内检测到移动输入时发送，
	UFUNCTION()
	void OnMovementCancelNotifyReceived(FGameplayEventData Payload);

	float DefaultGravityScale = 1.0f;

	// 记录引擎默认重力是否已缓存
	bool bGravityDefaultCached = false;

	// 激活后Montage会朝向锁定目标，并应用设定的索敌距离
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bRotateToLockTarget = false;

	// 无锁定目标但有移动输入时：只 warp 旋转（转向输入方向），位移交回动画自身的根位移。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (EditCondition = "bRotateToLockTarget"))
	bool bRotateToInputWhenNoTarget = false;

	// warp target 名称，须与攻击 Montage 里 AnimNotifyState_MotionWarping 的 WarpTargetName 一致
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (EditCondition = "bRotateToLockTarget"))
	FName LockOnWarpTargetName = TEXT("AttackFacing");

	// 落点相对目标中心的退让距离（cm），沿「目标 → 自身」方向退开。
	// 应 ≥ 双方胶囊半径之和，否则落点落在目标胶囊内部：地面贴身攻击靠碰撞挤开无妨，
	// 但空中下落类攻击会先被水平拖到目标正上方、再垂直落到胶囊顶面并顺坡滑走。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (EditCondition = "bRotateToLockTarget", ClampMin = "0.0"))
	float LockOnWarpStandoff = 20.f;
	

	// ── 穿透距离（Forward Overshoot）─────────────────────────────
	// 开启后额外写入一个独立命名的 warp target：落点为「目标位置 + 冲刺方向 × OvershootDistance」，
	// 供「穿过目标落到身后」这类动画使用。Montage 里对应 NMS 的 WarpTargetName 要填
	// ForwardOvershootTargetName；它与 AttackFacing 区间互斥使用（同一时刻只让一段 NMS 生效）。
	// 方向与落点在该区间开头算一次后固定，不逐帧跟随目标——否则穿身后「角色→目标」反向会让落点翻到另一侧。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn|ForwardOvershoot")
	bool bEnableForwardOvershoot = false;

	// 穿透区间使用的 warp target 名（与攻击朝向的 AttackFacing 区分）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn|ForwardOvershoot", meta = (EditCondition = "bEnableForwardOvershoot"))
	FName ForwardOvershootTargetName = TEXT("ForwardOvershoot");

	// 穿到目标身后多远（cm）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn|ForwardOvershoot", meta = (EditCondition = "bEnableForwardOvershoot"))
	float OvershootDistance = 200.f;

	// 本次穿透位移的上限（cm）
	UPROPERTY(EditDefaultsOnly, Category = "LockOn|ForwardOvershoot", meta = (EditCondition = "bEnableForwardOvershoot"))
	float MaxOvershootWarpDist = 600.f;

	// 穿透区间生效期间挂载的状态 Tag：区间开头判定「落点越过目标」时置位，供 ANS_CombatCamera
	// 之类做条件触发；区间结束或 GA 结束兜底清除。留空则不挂。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn|ForwardOvershoot", meta = (EditCondition = "bEnableForwardOvershoot"))
	FGameplayTag ForwardOvershootStateTag;

	// 穿透区间的方向与落点
	struct FForwardOvershootPoint
	{
		FVector DashDir = FVector::ZeroVector;
		FVector WarpLocation = FVector::ZeroVector;
		// 落点是否越过目标（即本次会穿身）
		bool bPassThroughTarget = false;
	};

	// 按当前锁定目标算出本段穿透的方向与落点（含位移上限钳制）。返回的 DashDir 长度为 0 表示无有效方向。
	FForwardOvershootPoint ComputeForwardOvershootPoint(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget) const;

	// 同步穿透条件 Tag（用 count 置 0/1，避免 loose tag 计数累加残留）
	void SetForwardOvershootStateTag(bool bActive);

	// 按当前状态（锁定目标 / 移动输入）写入 warp target
	void UpdateLockOnWarpTarget();

	// 写入穿透区间的 warp target；区间首次出现时算一次方向与落点并缓存
	void UpdateForwardOvershootWarpTarget(const AExtraPlayerCharacter* PlayerChar, UMotionWarpingComponent* MWC, const AActor* LockTarget);

	// 每个 MW modifier 的位移/旋转开关在 NMS 区间开始时复制而来。
	// 这里记录首次见到的原始值作为基准，每帧只在基准之上做「关闭」，不主动「开启」——
	// 记录前后摇区间在 NMS 的勾选设置，如果取消 Warp Translation 就能只转向不追击。
	TMap<TWeakObjectPtr<URootMotionModifier_Warp>, TPair<bool, bool>> WarpSwitchBaseline;

	// 每个穿透区间（modifier）开头锁定的方向与落点
	TMap<TWeakObjectPtr<URootMotionModifier_Warp>, FForwardOvershootPoint> ForwardOvershootCache;

	// MW 每帧更新 modifier 之前的回调，是设置 modifier 开关的时机
	// （NMS 区间开始时才创建 modifier 并把开关拷回默认值，不能只在激活时设一次）
	UFUNCTION()
	void OnMotionWarpingPreUpdate(UMotionWarpingComponent* MotionWarpingComp);
	
	// 是否启用武器碰撞伤害响应：开启后，服务端监听 GetDamageEventTag() 的命中事件，
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effect")
	bool bEnableWeaponDamage = false;

	// 通用伤害 GE（子类蓝图默认值配置；若需按上下文选 GE，覆写 GetDamageEffect）
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effect")
	TSubclassOf<UGameplayEffect> DefaultWeaponDamageEffect;

	// 挂载武器伤害监听
	void SetupDamageListener();

	// 当前 GA 响应的伤害事件 Tag（默认通用 ability.damage，子类可覆写为专属 Tag）
	virtual FGameplayTag GetDamageEventTag() const;

	// 本次伤害使用的 GE（默认 DefaultDamageEffect，子类可覆写按上下文选择）
	virtual TSubclassOf<UGameplayEffect> GetDamageEffect() const;

	// 对碰撞目标批量应用伤害（通用实现）。
	// 子类如需追加独有逻辑（击飞/附加效果），先调用 Super::DoDamage(Data)。
	virtual void DoDamage(const FGameplayEventData& Data);

	// ── 攻击段配置（伤害多段 / 能量 / 表现）────────────────────
	// key = Montage Section 名（无 Section 时用 Montage 资产名）。
	// 未配置的 GA 回退旧的单段 DoDamage 路径，行为不变。
	UPROPERTY(EditDefaultsOnly, Category = "Damage|Segment")
	TMap<FName, FAttackSegmentConfig> AttackSegmentConfigs;

	// 统一伤害结算入口：把配置展开成多段，按 SegmentInterval 调度，目标在命中瞬间锁定。
	// WindowId 非空时优先取配置里对应的伤害窗口段，找不到回退整段配置。
	// DoDamage / 范围伤害共用；投射物后续也可复用。
	void ApplyDamageProfile(const TArray<AActor*>& Targets, const FAttackSegmentConfig& Config, FName WindowId = NAME_None);

	// 当前 Section 对应的攻击段配置；未配置返回 nullptr
	const FAttackSegmentConfig* FindAttackSegmentConfig() const;

	// 能量去重：同一 Montage/Section 只返回一次 Value
	float ClaimEnergyValue(float Value);

	// 单个目标的伤害段调度状态（多目标时各自独立推进，互不覆盖）
	struct FDamageSequence
	{
		TWeakObjectPtr<AActor> Target;
		TArray<FDamageSegment> Segments;   // Count 已展开成多条
		int32 NextIndex = 0;
		float Interval = 0.f;
		bool bSendHitReact = true;
		// 一次命中序列只允许第一个击飞段生效，后续段即使勾了击飞也抑制
		bool bKnockDownUsed = false;
		FTimerHandle TimerHandle;
	};

	TArray<TSharedPtr<FDamageSequence>> ActiveDamageSequences;

	void ApplyNextDamageSegment(TWeakObjectPtr<AActor> Target);
	void ClearDamageSequences();

	// 能量数值由 AttackSegmentConfigs 的 EnergyGain 提供；这里只保留资格门控（留空 = 不限）。
	UPROPERTY(EditDefaultsOnly, Category = "Energy")
	FGameplayTagContainer EnergyGainRequiredTags;

	// 当前攻击段配置的能量值（已检查 RequiredTags）；未配置返回 0
	float GetConfiguredEnergyGain() const;

	virtual UAnimMontage* GetEnergyGainMontage() const;
	virtual FName GetEnergyGainSection() const;

	// 权威端给自身累积能量（封顶由属性集按 EnergyMaxValue 处理）
	void AddEnergyValue(float Delta);

	// 能量去重：本次结算对应的是哪一段攻击（Montage + Section）。
	// 同一段内武器轨迹 / 范围伤害多次进入 DoDamage 时只结算一次；换段即视为新的一次攻击。
	UPROPERTY()
	TObjectPtr<UAnimMontage> LastEnergyGainMontage = nullptr;

	FName LastEnergyGainSection = NAME_None;

	bool bEnergyGainedForCurrentAttack = false;

	// ── 角色中心范围伤害（事件帧驱动）─────────────────────
	// 是否启用范围伤害：开启后服务端监听 GetAreaDamageTriggerTag() 的 GameplayEvent，
	// Montage 伤害帧放一个 AN_AreaCheck 触发一次范围判定（一个 AN = 一次）。
	// 圆心默认取角色位置，AN 可通过 FAreaCheckData 提供 XY 偏移与半径覆写；
	// 对半径内全部敌方存活单位统一应用 GetDamageEffect() 选出的伤害 GE，
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage")
	bool bEnableAreaDamage = false;

	// 范围检测半径兜底值（cm）：AN_AreaCheck 未指定半径（<=0）时使用；两者都 <=0 则仅告警不结算。
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage", meta = (ClampMin = "0.0", EditCondition = "bEnableAreaDamage"))
	float AreaDamageRadius = 300.f;
	
	// 圆心是否采用「锁定目标位置」而非角色位置：
	// 仅当 AN_AreaCheck 的 CenterMode 为 Inherit（默认）时生效；无锁定目标则回退角色位置。
	// 单个 AN 想脱离此圆心配置自行指定，把该 AN 的 CenterMode 改成 Owner / LockTarget 。
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage", meta = (EditCondition = "bEnableAreaDamage"))
	bool bAreaDamageUseLockTargetAsCenter = false;

	// 范围 Debug：开启后 PerformAreaDamage 画「地面脚印圈(半径) + 判定球 + 命中连线/打点」，
	// 并在屏幕打印当前半径与命中数——可直接目测范围大概有多大。GA 蓝图 Class Defaults 里勾选。
	UPROPERTY(EditDefaultsOnly,Category = "Debug")
	bool bShouldDrawDebug = false;

	// 范围伤害触发事件 Tag（默认ability.area.damage；）
	virtual FGameplayTag GetAreaDamageTriggerTag() const;

	// 收集圆心半径内敌方存活单位并统一结算伤害（Debug 开启时附带可视化）。
	// CenterMode 决定圆心：Inherit→按 bAreaDamageUseLockTargetAsCenter 解析；Owner→角色位置+CenterOffset（XY 偏移，Z 忽略）；
	// LockTarget→锁定目标位置（无锁定回退角色位置）。Radius <=0 时回退到 AreaDamageRadius。
	// 内部事件回调调用；子类也可在无 Notify 的时机手动触发（注意只应权威端执行）。
	void PerformAreaDamage(const FVector& CenterOffset = FVector::ZeroVector, float Radius = 0.f,
	                       EAreaCenterMode CenterMode = EAreaCenterMode::Inherit, FName WindowId = NAME_None);

	// 挂载范围伤害事件监听
	void SetupAreaDamageListener();

	// 范围伤害事件回调，解析负载中的 FAreaCheckData（可缺省）后转发到 PerformAreaDamage
	UFUNCTION()
	void OnAreaDamageEventReceived(FGameplayEventData Payload);

	// 内部：Debug 可视化（地面脚印圈 + 判定球 + 命中连线/打点 + 屏幕打印半径/命中数）
	void DrawAreaDamageDebug(const FVector& Center, float Radius, const TArray<AActor*>& Targets);

	//获得AvatarCharacter，即Push对象
	AExtraPlayerCharacter* GetOwningAvatarCharacter();

private:
	// 武器伤害事件回调（动态委托目标，转发到 virtual DoDamage 供子类覆写）
	UFUNCTION()
	void OnDamageEventReceived(FGameplayEventData Data);

	UPROPERTY()
	TObjectPtr<AExtraPlayerCharacter> AvatarCharacter;
};
