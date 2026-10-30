#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "GameplayTagContainer.h"
#include "ExtraGameplayTypes.h"
#include "ExtraAbilityBase.generated.h"

/**
 * GA基类。
 *   1) Uninterruptible 霸体窗口
 *   2) CancelWindow 后摇可打断窗口
 */
UCLASS(Abstract)
class EXTRACTGAMECHARACTER_API UExtraAbilityBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UExtraAbilityBase();

	virtual void PreActivate(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, FOnGameplayAbilityEnded::FDelegate* OnGameplayAbilityEndedDelegate,
		const FGameplayEventData* TriggerEventData = nullptr) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual bool CommitAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) override;

	// 对 Avatar 施加推力（底层 LaunchCharacter）。bOverrideXY / bOverrideZ 控制是否覆盖对应轴的速度
	void PushSelf(const FVector& PushVel, bool bOverrideXY = true, bool bOverrideZ = true);

protected:
	// ── 后摇可打断窗口（CancelWindow）─────────────────────────────
	// 开启后：Montage 后摇段放 AN_CancelWindow，进窗即「视为该 GA 已取消」——
	//   1) 撤销自身 BlockAbilitiesWithTag 封锁（此前被它挡住的 GA 重新可激活，如自身同类）;
	//   2) 登记为窗口持有者，任何 GA 在 CommitAbility 时发现持有者即取消它;
	//   3) 移动输入仍走 GetMovementCancelTag() 事件即时打断;
	//   4) 出窗恢复封锁。
	UPROPERTY(EditDefaultsOnly, Category = "Movement | CancelWindow")
	bool bEnableCancelWindow = false;

	// 本 GA 提交成功时，是否可以取消正处于 CancelWindow 的其他 GA。
	// 默认 true = 「任何 GA 都能打断」；被动/工具类 GA 若不应打断表现段，置 false。
	UPROPERTY(EditDefaultsOnly, Category = "Movement | CancelWindow")
	bool bCanInterruptCancelWindow = true;

	// 监听 AN_CancelWindow 的开/关窗事件
	void SetupCancelWindowListener();

	// 进窗：撤销自身Block + 登记持有者（加入ASC中特别维护的数组）
	void EnterCancelWindow();

	// 出窗 / GA 结束兜底：恢复Block + 解除登记
	void ExitCancelWindow();

	// 开窗事件回调
	UFUNCTION()
	void OnCancelWindowBeginReceived(FGameplayEventData Payload);

	// 关窗事件回调
	UFUNCTION()
	void OnCancelWindowEndReceived(FGameplayEventData Payload);

	// 本次激活是否处于窗口内
	bool bInCancelWindow = false;

	// ── 霸体窗口（Uninterruptible）──────────────────────────────
	// 激活时以 loose tag 形式把 UninterruptibleTag 加入 ASC owned tags（表现动画段），
	// 后摇段必须由 AN_EndUninterruptible 发送事件移除，从而放开其他 GA 通过 CancelAbilitiesWithTag 打断后摇。
	UPROPERTY(EditDefaultsOnly, Category = "Uninterruptible")
	bool bEnableUninterruptible = false;

	// 霸体 tag（默认 State.Uninterruptible）
	UPROPERTY(EditDefaultsOnly, Category = "Uninterruptible", meta = (EditCondition = "bEnableUninterruptible"))
	FGameplayTag UninterruptibleTag;

	// 本次激活是否霸体
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

	// ── 推力（Push）──────────────────────────────────────────
	// 本 GA 激活期间统一监听 Push_Self 事件，Montage 里放 AN_ApplyPush 在指定帧发一次，
	// AN 带 FPushTargetData（推力向量 + XY/Z 覆盖标志），这里解析后作用到自身。

	// Push_Self 事件回调：解析负载里的 FPushTargetData 后调用 PushSelf，使用AN_ApplyPush触发
	UFUNCTION()
	void OnPushSelfNotifyReceived(FGameplayEventData Payload);

	// 挂载 Push_Self 事件监听（PreActivate 自动调用）
	void SetupPushSelfListener();

	// 从 TargetData 中取出 FPushTargetData，成功则返回true
	static bool ExtractPushData(const FGameplayAbilityTargetDataHandle& Handle, FPushTargetData& OutData);

	// 推单个目标：给目标发 LaunchedAbilityActivation 事件，方向以 ImpactNormal 承载
	static void PushTarget(AActor* Target, const FVector& PushVel);

	// 对群 Push
	void PushTargets(const TArray<AActor*>& Targets, const FVector PushVel);

	// 解析 TargetData 里所有 Actor 并施加 Push
	void PushTargets(const FGameplayAbilityTargetDataHandle& TargetDataHandle, const FVector& PushVel);

	// 由 Targets 与 FromLocation 算出方向后施加 Push
	void PushTargetsFromLocation(const FGameplayAbilityTargetDataHandle& TargetDataHandle, const FVector& FromLocation, float PushSpeed);

	// 同上，但 FromLocation 取 AvatarActor 的位置
	void PushTargetsFromOwnerLocation(const TArray<AActor*>& Targets, float PushSpeed);

	void PushTargetsFromLocation(const TArray<AActor*>& Targets, const FVector& FromLocation, float PushSpeed);
};
