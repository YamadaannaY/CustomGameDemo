#pragma once

#include "CoreMinimal.h"
#include "ExtraPassiveGameplayAbility.h"
#include "Engine/EngineTypes.h"
#include "GA_KnockDown.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
struct FPushTargetData;

/**
 * 受击后倒地效果，被动GA。
 * 触发：攻击 GA 命中目标时发 ability.knockdown（负载带推力参数），由 AbilityTriggers 收到并激活此GA
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

	// 激活瞬间施加击飞推力（参数由攻击方的伤害段经 GE 参数带到受击侧被动倒地GA的TriggerEventData中）
	void ApplyKnockDownPush(const FPushTargetData& PushData);

	// 通过 Multicast 在各端本地施加推力：
	// 本地控制角色只在服务端推会被客户端 ServerMove 覆盖，必须两端各自执行
	void ApplyPushToAvatar(const FVector& Velocity, bool bOverrideXY, bool bOverrideZ);

	// 延迟施加推力：激活流程里直接推可能被同帧移动更新压掉
	void SchedulePush(const FVector& Velocity, bool bOverrideXY, bool bOverrideZ);

	// 击飞推力的延迟施加时间
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|Push", meta = (ClampMin = "0.0"))
	float PushDelay = 0.1f;

	FVector PendingPushVelocity = FVector::ZeroVector;
	bool bPendingPushOverrideXY = false;
	bool bPendingPushOverrideZ = true;
	// 同一次倒地只认第一次击飞，避免后续伤害段覆盖首段数值
	bool bPushScheduled = false;
	FTimerHandle PushTimerHandle;

	// ── 倒地中再次受击 ────────────────────────────────────────
	// 倒地 Start/Loop 阶段再次被击中：用固定的上方速度轻推并重播起手段，
	// 不沿用攻击本身的击飞参数。次数上限内有效，起身阶段不再触发。
	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|DownHit")
	FVector DownHitVelocity = FVector(0.0f, 0.0f, 200.0f);

	UPROPERTY(EditDefaultsOnly, Category = "KnockDown|DownHit", meta = (ClampMin = "0"))
	int32 MaxDownHits = 3;

	int32 DownHitCount = 0;

	void SetupDownHitListener();

	UFUNCTION()
	void OnDownHitReceived(FGameplayEventData Payload);

	// 起手段播放任务：重受击重播时需要先清理旧任务
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> PlayStartTask;

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
