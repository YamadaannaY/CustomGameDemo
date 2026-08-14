#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "ExtraGameplayAbility.generated.h"

class UAnimMontage;

/**
 * 自定义 GA 基类
 *
 * 所有武器技能 GA 蓝图的父类应设为此类。
 * 触发方式：各 GA 在构造函数中通过 AbilityTriggers（GameplayEvent + InputTag）声明
 * 自己响应的输入，废弃旧的 InputID 机制。
 *
 * 移动打断（Movement Cancel）机制：
 *   - 子类在 ActivateAbility 中调用 SetupMovementCancel() 开始监听取消事件
 *   - 需要打断时，AnimNotify 发送 GetMovementCancelTag()（"ability.cancel"）事件
 *   - 收到事件后：有移动输入则立即结束；无输入则开定时器轮询，出现移动输入再结束
 *   - 子类需覆写 GetActiveMontageForCancel() 返回当前可被打断的 Montage
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UExtraGameplayAbility();

	UAnimInstance* GetOwnerAnimInstance() const;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditAnywhere,Category="Weapon | Visible")
	bool ClearWeaponShowOnAbilityEnd = true ;

	// ── 移动打断（Movement Cancel）──────────────────────────

	// 是否启用移动打断机制
	UPROPERTY(EditDefaultsOnly, Category = "Movement Cancel")
	bool bEnableMovementCancel = false;

	// 移动打断时 Montage 的淡出时间
	UPROPERTY(EditDefaultsOnly, Category = "Movement Cancel")
	float MovementCancelBlendOutTime = 0.1f;

	// 子类在 ActivateAbility 中调用，开始监听取消事件
	void SetupMovementCancel();

	// 子类覆写，返回当前在播、可被移动打断的 Montage
	virtual UAnimMontage* GetActiveMontageForCancel() const { return nullptr; }

	// 取消事件 Tag（默认 "ability.cancel"，子类可覆写）
	virtual FGameplayTag GetMovementCancelTag() const;

	// 取消事件回调（AnimNotify 发送 GetMovementCancelTag 触发）
	UFUNCTION()
	void OnMovementCancelNotifyReceived(FGameplayEventData Payload);

	// 定时轮询是否有移动输入，命中则打断
	void CheckMovementInputForCancel();

	// 检查玩家是否有移动输入
	bool HasMovementInput() const;

	// 定时器句柄
	FTimerHandle MovementCheckTimerHandle;

	// 是否因移动输入触发 EndAbility（用于区分 BlendOut 时间）
	bool bEndingFromMovement = false;
};
