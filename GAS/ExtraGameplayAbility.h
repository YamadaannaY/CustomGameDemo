#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "ExtraGameplayAbility.generated.h"

class UAnimMontage;

/**
 * 自定义 GA 基类
 *
 * 所有武器GA的蓝图父类应设为此类。
 * 
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

protected:
	//默认在所有GA结束时将所有Weapon统一再次进行ClearShow操作
	UPROPERTY(EditAnywhere,Category="Weapon | Visible")
	bool ClearWeaponShowOnAbilityEnd = true ;

	// 是否启用移动打断机制（开启此项后，使用ability.cancel可以提前结束GA）。
	// 只需在子类构造函数中置 true，基类会在 PreActivate 自动挂载监听，无需在 ActivateAbility 里手动调用。
	UPROPERTY(EditDefaultsOnly, Category = "Movement Cancel")
	bool bEnableMovementCancel = false;

	UPROPERTY(EditDefaultsOnly, Category = "Movement Cancel")
	float MontageCancelBlendOutTime = 0.3f ;

	// 开始监听取消事件（由 PreActivate 自动调用，子类无需手动触发）
	void SetupMovementCancel();

	// 子类覆写，返回当前在播、可被移动打断的 Montage
	virtual UAnimMontage* GetActiveMontageForCancel() const { return nullptr; }

	// 命中移动打断的瞬间回调。
	virtual void OnMovementCancelTriggered() {}

	// 取消事件 Tag（默认 "ability.cancel"，子类可覆写）
	virtual FGameplayTag GetMovementCancelTag() const;

	// 取消事件回调。事件由 AN_CancelWindow 在区间内检测到移动输入时发送，
	// 到达这里即表示应当打断，无需再轮询输入。
	UFUNCTION()
	void OnMovementCancelNotifyReceived(FGameplayEventData Payload);

	// 是否因移动输入触发 EndAbility（决定是否停止当前 Montage；停止时使用 Montage 自身 BlendOut 时长）
	bool bEndingFromMovement = false;
};
