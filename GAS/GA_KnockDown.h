#pragma once

#include "CoreMinimal.h"
#include "ExtraPassiveGameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "GA_KnockDown.generated.h"

class UAnimMontage;

/**
 * 受击连段后倒地。
 * 触发：GA_HitReact 判定受击窗口内累计次数达阈值后，把 ability.knockdown 发回自身 *
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_KnockDown : public UExtraPassiveGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_KnockDown();

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	virtual void OnReactActivated(const FGameplayEventData* TriggerEventData) override;

	// ── 三段动画 ──────────────────────────────────────────
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|Montage")
	TObjectPtr<UAnimMontage> KnockDownStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|Montage")
	TObjectPtr<UAnimMontage> KnockDownLoopMontage;

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|Montage")
	TObjectPtr<UAnimMontage> KnockDownEndMontage;
	
	// 落地检测兜底轮询间隔（LandedDelegate 在「激活瞬间已贴地」时不触发）
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|Land")
	float LandCheckInterval = 0.08f;

private:
	UFUNCTION()
	void OnStartMontageFinished();

	UFUNCTION()
	void OnStartMontageInterrupted();

	UFUNCTION()
	void OnLandDetected(const FHitResult& Hit);

	UFUNCTION()
	void PollLandCheck();

	UFUNCTION()
	void OnLoopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void OnEndMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void PlayStartMontage();
	void PlayLoopMontage();
	void PlayEndMontage();
	void StopLoopMontage();

	// 落地委托与轮询共用的入口，带阶段与 IsFalling 双重校验
	void TryTriggerLand();
	
	// 被动基类没有 GetOwnerAnimInstance（那个在主动基类上），这里自己取
	UAnimInstance* GetAvatarAnimInstance() const;

	// 当前相位：用于区分「主动换段」与「外部打断」——主动换段时回调会因相位已变而直接返回
	enum class EKnockDownPhase : uint8
	{
		None,
		Start,
		Loop,
		End
	};

	EKnockDownPhase CurrentPhase = EKnockDownPhase::None;

	// 是否已经离开过地面。击飞是地面起手，LaunchCharacter 当帧还不会把角色切到 Falling，
	// 没有这个门闩的话起手那一瞬就会被落地检测判成「已落地」而直接跳进 End 段
	bool bHasLeftGround = false;

	FTimerHandle LandCheckTimerHandle;
};
