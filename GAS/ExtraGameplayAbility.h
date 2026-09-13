#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/EngineTypes.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayTypes.h"
#include "ExtraGameplayAbility.generated.h"

class UAnimMontage;
class UCharacterMovementComponent;
class UGameplayEffect;
class UMotionWarpingComponent;

/**
 * 自定义GA基类
 * 所有GA的蓝图父类应设为此类。
 * GA通用逻辑、配置于此处实现
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UExtraGameplayAbility();

	UAnimInstance* GetOwnerAnimInstance() const;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 覆写 PreActivate：在 GA 激活的前置阶段（ActivateAbility 之前）统一挂载移动打断监听。
	// 子类只需在构造函数里置 bEnableMovementCancel = true，无需再在 ActivateAbility 里手动调用 SetupMovementCancel。
	virtual void PreActivate(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate, const FGameplayEventData* TriggerEventData = nullptr) override;

	// 覆写 CommitAbility：提交成功后，若当前有 GA 处于 CancelWindow（后摇可打断窗口），取消它。
	// 放在提交之后而非 PreActivate，是为了避免本 GA 因消耗/冷却提交失败却白白打断对方。
	virtual bool CommitAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, FGameplayTagContainer* OptionalRelevantTags) override;

	// 对 Avatar 施加推力（底层 LaunchCharacter）。
	// bOverrideXY / bOverrideZ 控制是否覆盖对应轴的速度：
	//   空中二段跳应 bOverrideZ=true、bOverrideXY=false，只改竖直、保留水平速度；
	//   需要强力击飞（清掉原有速度）时再同时置 true。
	// public：供 AnimNotify（如 AN_ApplyPush）在动画帧回调里对当前激活 GA 施加推力。
	void PushSelf(const FVector& PushVel, bool bOverrideXY = true, bool bOverrideZ = true);

protected:
	//默认在所有GA结束时将所有Weapon统一再次进行ClearShow操作
	UPROPERTY(EditAnywhere,Category="Weapon | Visible")
	bool ClearWeaponShowOnAbilityEnd = true ;

	// 是否启用移动打断机制（开启此项后，使用ability.cancel可以提前结束GA）。
	// 只需在子类构造函数中置 true，基类会在 PreActivate 自动挂载监听，无需在 ActivateAbility 里手动调用。
	UPROPERTY(EditDefaultsOnly, Category = "Movement | Cancel")
	bool bEnableMovementCancel = false;
	
	UPROPERTY(EditDefaultsOnly,Category= "MoveMent | MotionWarp")
	float MotionWarpMaxMoveDist = 150.f  ; 

	// 是否启用重力缩放：开启后，GA 激活时把角色移动组件的 GravityScale 设为 AbilityGravityScale，
	// EndAbility 时自动恢复为激活前的原始值。与 bEnableMovementCancel 一样在 PreActivate 统一处理。
	UPROPERTY(EditDefaultsOnly, Category = "Gravity Scale")
	bool bEnableGravityScale = false;

	// 激活期间的角色重力系数：0 = 无重力（匀速下落），1 = 引擎默认重力（持续加速）。
	UPROPERTY(EditDefaultsOnly, Category = "Gravity Scale", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableGravityScale"))
	float AbilityGravityScale = 1.0f;

	// 开始监听取消事件（由 PreActivate 自动调用，子类无需手动触发）
	void SetupMovementCancel();

	// 子类覆写，返回当前在播且可被移动打断的 Montage
	virtual UAnimMontage* GetActiveMontageForCancel() const { return nullptr; }

	// 命中移动打断的瞬间回调。
	virtual void OnMovementCancelTriggered() {}

	// ── 后摇可打断窗口（CancelWindow）─────────────────────────────
	// 开启后：Montage 后摇段放 AN_CancelWindow，进窗即「视为该 GA 已取消」——
	//   1) 撤销自身 BlockAbilitiesWithTag 封锁（此前被它挡住的 GA 重新可激活，如自身同类）；
	//   2) 登记为窗口持有者，任何 GA 在 CommitAbility 时发现持有者即取消它；
	//   3) 移动输入仍走 GetMovementCancelTag() 事件即时打断。
	// 出窗恢复封锁。子类只需在构造函数置 true，其余由基类在 PreActivate / EndAbility 统一处理。
	UPROPERTY(EditDefaultsOnly, Category = "Movement | CancelWindow")
	bool bEnableCancelWindow = false;

	// 本 GA 提交成功时，是否可以取消正处于 CancelWindow 的其他 GA。
	// 默认 true = 「任何 GA 都能打断」；被动/工具类 GA 若不应打断表现段，置 false 退出。
	UPROPERTY(EditDefaultsOnly, Category = "Movement | CancelWindow")
	bool bCanInterruptCancelWindow = true;

	// 监听 AN_CancelWindow 的开/关窗事件（PreActivate 自动调用）
	void SetupCancelWindowListener();

	// 进窗：撤销自身封锁 + 登记持有者（幂等，重复进窗无副作用）
	void EnterCancelWindow();

	// 出窗 / GA 结束兜底：恢复封锁 + 解除登记（幂等）
	void ExitCancelWindow();

	// 开窗事件回调
	UFUNCTION()
	void OnCancelWindowBeginReceived(FGameplayEventData Payload);

	// 关窗事件回调
	UFUNCTION()
	void OnCancelWindowEndReceived(FGameplayEventData Payload);

	// 本次激活是否处于窗口内（幂等判断 + 调试用）
	bool bInCancelWindow = false;

	//根据Vel方向向量参数对单施加一个Push效果
	static void PushTarget(AActor*Target,const FVector& PushVel);
	
	//对群Push效果
	void PushTargets(const TArray<AActor*>& Targets,const FVector PushVel);
	
	//解析TA提供的TargetData中所有Actors并施加Push效果
	void PushTargets(const FGameplayAbilityTargetDataHandle& TargetDataHandle,const FVector& PushVel);
	
	//从Handle获取Targets根据Loc位置向量参数计算得到方向，施加Push效果
	void PushTargetsFromLocation(const FGameplayAbilityTargetDataHandle& TargetDataHandle, const FVector& FromLocation ,float PushSpeed);
	
	
	//对象为Actors，封装Loc为AvatarActor的位置
	void PushTargetsFromOwnerLocation(const TArray<AActor*>& Targets,float PushSpeed);
	
	void PushTargetsFromLocation(const TArray<AActor*>& Targets, const FVector& FromLocation , float PushSpeed);
	
	// 取消事件 Tag（默认 "ability.cancel"，子类可覆写）
	virtual FGameplayTag GetMovementCancelTag() const;

	// 取消事件回调。事件由 AN_CancelWindow 在区间内检测到移动输入时发送，
	UFUNCTION()
	void OnMovementCancelNotifyReceived(FGameplayEventData Payload);

	// Push_Self 事件回调：AN_ApplyPush 发送的推力（含向量 + 覆盖标志），解析后调用 PushSelf。
	// 在 PreActivate 统一挂载监听，任何激活中的 GA 都能响应动画帧推力。
	UFUNCTION()
	void OnPushSelfNotifyReceived(FGameplayEventData Payload);

	// 挂载 Push_Self 事件监听（由 PreActivate 统一调用）
	void SetupPushSelfListener();

	// 是否因移动输入触发 EndAbility（决定是否停止当前 Montage；停止时使用 Montage 自身 BlendOut 时长）
	bool bEndingFromMovement = false;

	// 引擎默认重力：首次启用重力缩放的激活时，从移动组件缓存一次（此时尚未被任何 GA 修改），
	// EndAbility 永远恢复为该默认值，而非激活前那一刻的值，避免多个重力 GA 连续/嵌套时把非 1 的中间值固化。
	float DefaultGravityScale = 1.0f;

	// 引擎默认重力是否已缓存（实例复用 + 只在第一次GA被调用前读取一次）
	bool bGravityDefaultCached = false;

	// 是否启用霸体窗口：激活时以 loose tag 形式把 UninterruptibleTag 加入 ASC owned tags（表现动画段），
	// 后摇段必须由 AN_EndUninterruptible 发送事件移除，从而放开其他 GA 通过 CancelAbilitiesWithTag 打断后摇。
	// 子类只需在构造函数里置 bEnableUninterruptible = true，其余由基类在 PreActivate / EndAbility 统一处理。
	UPROPERTY(EditDefaultsOnly, Category = "Uninterruptible")
	bool bEnableUninterruptible = false;

	// 霸体 tag（默认 State.Uninterruptible，作为 ActivationBlockedTags 供被打断方阻断用）
	UPROPERTY(EditDefaultsOnly, Category = "Uninterruptible", meta = (EditCondition = "bEnableUninterruptible"))
	FGameplayTag UninterruptibleTag;

	// 本次激活是否已挂载霸体 tag（防止后摇已放开后 EndAbility 兜底重复 Remove 造成负计数）
	bool bUninterruptibleActive = false;

	// 以 loose tag 形式挂载霸体 tag 到 ASC（PreActivate 自动调用）
	void ApplyUninterruptibleTag();

	// 移除 loose tag（由 AN_EndUninterruptible 事件触发，或 EndAbility 兜底清理）
	void ReleaseUninterruptible();

	// 监听霸体结束事件（PreActivate 自动调用）
	void SetupUninterruptibleReleaseListener();

	// 霸体结束事件回调：移除 loose tag，放开后摇打断
	UFUNCTION()
	void OnUninterruptibleReleaseReceived(FGameplayEventData Payload);


	// ── 攻击朝向（MR）───────────────────────────────────
	// 激活时对攻击 Montage 设置 MotionWarping warp target，动画内由
	// AnimNotifyState_MotionWarping 区间完成平滑转向。
	// 子类只需在构造函数置 true，其余由基类在 PreActivate / EndAbility 统一处理。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn")
	bool bRotateToLockTarget = false;

	// 无锁定目标但有移动输入时：只 warp 旋转（转向输入方向），位移交回动画自身的根位移。
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (EditCondition = "bRotateToLockTarget"))
	bool bRotateToInputWhenNoTarget = false;

	// warp target 名称，须与攻击 Montage 里 AnimNotifyState_MotionWarping 的 WarpTargetName 一致
	UPROPERTY(EditDefaultsOnly, Category = "LockOn", meta = (EditCondition = "bRotateToLockTarget"))
	FName LockOnWarpTargetName = TEXT("AttackFacing");

	// 按当前状态（锁定目标 / 移动输入）写入 warp target，并同步 modifier 上的位移、旋转开关
	void UpdateLockOnWarpTarget();

	// 有锁定目标时的 warp 落点计算；bOutWarpTranslation 指示本次是否做位移 warp。
	// 默认：落点在目标位置（超出 MotionWarpMaxMoveDist 时钳制到该距离处）并做位移。
	// 特化 GA（如居合前冲的「穿过目标落到身后」）可覆写。
	virtual FVector ComputeLockOnWarpLocation(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget, bool& bOutWarpTranslation) const;

	// 有锁定目标时的 warp 朝向。默认朝目标。
	// 特化可覆写（如居合前冲锁定起手方向，避免穿过目标后方向反转导致落点跳变）。
	virtual FVector ComputeLockOnFaceDir(const AExtraPlayerCharacter* PlayerChar, const AActor* LockTarget, const FVector& DirToTarget) const;

	// MW 每帧更新 modifier 之前的回调，是设置 modifier 开关的时机
	// （NMS 区间开始时才创建 modifier 并把开关拷回默认值，不能只在激活时设一次）
	UFUNCTION()
	void OnMotionWarpingPreUpdate(UMotionWarpingComponent* MotionWarpingComp);


	// ── 通用武器碰撞伤害 ──────────────────────────────────────
	// 是否启用武器碰撞伤害响应：开启后，服务端监听 GetDamageEventTag() 的命中事件，
	// 对 TargetData 中每个目标应用 GetDamageEffect() 选出的 GE。
	// 攻击 GA 只需在构造函数置 true 即可获得通用伤害；非攻击 GA（如 Evade）保持 false。
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effect")
	bool bEnableWeaponDamage = false;

	// 通用伤害 GE（子类蓝图默认值配置；若需按上下文选 GE，覆写 GetDamageEffect）
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effect")
	TSubclassOf<UGameplayEffect> DefaultWeaponDamageEffect;

	// 挂载武器伤害监听（由 PreActivate 自动调用，子类无需手动触发）
	void SetupDamageListener();

	// 当前 GA 响应的伤害事件 Tag（默认通用 ability.damage，子类可覆写为专属 Tag）
	virtual FGameplayTag GetDamageEventTag() const;

	// 本次伤害使用的 GE（默认 DefaultDamageEffect，子类可覆写按上下文选择）
	virtual TSubclassOf<UGameplayEffect> GetDamageEffect() const;

	// 对碰撞目标批量应用伤害（通用实现）。
	// 子类如需追加独有逻辑（击飞/附加效果），先调用 Super::DoDamage(Data) 再补充。
	virtual void DoDamage(const FGameplayEventData& Data);

	// ── 角色中心范围伤害（事件帧驱动）─────────────────────
	// 是否启用范围伤害：开启后服务端监听 GetAreaDamageTriggerTag() 的 GameplayEvent，
	// Montage 伤害帧放一个 AN_AreaCheck 触发一次范围判定（一个 AN = 一次）。
	// 圆心默认取角色位置，AN 可通过 FAreaCheckData 提供 XY 偏移与半径覆写；
	// 对半径内全部敌方存活单位统一应用 GetDamageEffect() 选出的伤害 GE，
	// 与武器轨迹伤害（bEnableWeaponDamage）可并存/二选一，共用 DoDamage 结算。
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage")
	bool bEnableAreaDamage = false;

	// 范围检测半径兜底值（cm）：AN_AreaCheck 未指定半径（<=0）时使用；两者都 <=0 则仅告警不结算。
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage", meta = (ClampMin = "0.0", EditCondition = "bEnableAreaDamage"))
	float AreaDamageRadius = 0.f;

	// 圆心是否默认采用「锁定目标位置」而非角色位置：
	// 仅当 AN_AreaCheck 的 CenterMode 为 Inherit（默认）时生效；无锁定目标则回退角色位置。
	// 单个 AN 想脱离本配置自行指定，把该 AN 的 CenterMode 改成 Owner / LockTarget 即可。
	UPROPERTY(EditDefaultsOnly, Category = "Area Damage", meta = (EditCondition = "bEnableAreaDamage"))
	bool bAreaDamageUseLockTargetAsCenter = false;

	// 范围 Debug：开启后 PerformAreaDamage 画「地面脚印圈(半径) + 判定球 + 命中连线/打点」，
	// 并在屏幕打印当前半径与命中数——可直接目测范围大概有多大。GA 蓝图 Class Defaults 里勾选。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Debug")
	bool bShouldDrawDebug = false;

	// 范围伤害触发事件 Tag（默认通用 ability.area.damage；子类如需专属 Tag 可覆写）
	virtual FGameplayTag GetAreaDamageTriggerTag() const;

	// 收集圆心半径内敌方存活单位并统一结算伤害（Debug 开启时附带可视化）。
	// CenterMode 决定圆心：Inherit→按 bAreaDamageUseLockTargetAsCenter 解析；Owner→角色位置+CenterOffset（XY 偏移，Z 忽略）；
	// LockTarget→锁定目标位置（无锁定回退角色位置）。Radius <=0 时回退到 AreaDamageRadius。
	// 内部事件回调调用；子类也可在无 Notify 的时机手动触发（注意只应权威端执行）。
	void PerformAreaDamage(const FVector& CenterOffset = FVector::ZeroVector, float Radius = 0.f,
	                       EAreaCenterMode CenterMode = EAreaCenterMode::Inherit);

	// 内部：PreActivate 统一挂载范围伤害事件监听
	void SetupAreaDamageListener();

	// 内部：范围伤害事件回调，解析负载中的 FAreaCheckData（可缺省）后转发到 PerformAreaDamage
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
