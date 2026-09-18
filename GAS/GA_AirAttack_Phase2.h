#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "ExtraGameplayAbility.h"
#include "GA_AirAttack_Phase2.generated.h"

class UAnimMontage;
class UGameplayEffect;
class AExtraSwordQi;
class UAbilitySystemComponent;

/**
 * 二阶段空中连打：本 GA 只负责前两段（Montage1 → Montage2）。
 *
 * 在第 2 段的可衔接窗口内再按一次输入时，本 GA 主动结束、把第三段交接给 GA_AirAttack
 * （下砸：起手 → 循环 → 落地）。两者共用 ability.basicattack.airattack，
 * 本 GA 的 BlockAbilitiesWithTag 保证交接完成前 GA_AirAttack 不会被激活。
 *
 * 段1 / 段2 期间落地播 LandMontage；第三段落地由 GA_AirAttack 自己的落地段处理。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UGA_AirAttack_Phase2 : public UExtraGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_AirAttack_Phase2();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	// 首段用动画1
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AttackMontage1;

	//第二段
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* AttackMontage2;

	// 落地段：落地帧播放
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	UAnimMontage* LandMontage;

	// 落地检测兜底轮询间隔
	UPROPERTY(EditDefaultsOnly, Category = "Land")
	float LandCheckInterval = 0.08f;

	// ── 剑气 ──────────────────────────────────────────────
	// 生成剑气用的 Actor 类（用蓝图子类挂 Niagara 特效、调判定盒尺寸）
	UPROPERTY(EditDefaultsOnly, Category = "SwordQi")
	TSubclassOf<AExtraSwordQi> SwordQiActorClass;

	// 剑在武器组里对应的 WeaponTag（经武器组件 GetWeaponMeshByTag 找到剑的 StaticMesh）
	UPROPERTY(EditDefaultsOnly, Category = "SwordQi")
	FGameplayTag SwordWeaponTag;

	// 出剑气的 Socket（挂在剑的 Mesh 上）
	UPROPERTY(EditDefaultsOnly, Category = "SwordQi")
	FName SwordQiSpawnSocketName;

	UPROPERTY(EditDefaultsOnly, Category = "SwordQi", meta = (ClampMin = "0.0"))
	float SwordQiSpeed = 2000.f;

	UPROPERTY(EditDefaultsOnly, Category = "SwordQi", meta = (ClampMin = "0.0"))
	float SwordQiLifeTime = 2.f;

	// 剑气命中的伤害 GE（留空 = 剑气只飞不造成伤害）
	UPROPERTY(EditDefaultsOnly, Category = "SwordQi")
	TSubclassOf<UGameplayEffect> SwordQiDamageEffect;

	// 当前推进到第几段（0 起）
	int32 StageIndex = 0;

	// 交接段号：推进到第 2 段（索引 1）后再收到输入，即交接给 GA_AirAttack 打第三段
	static constexpr int32 HandoffStageIndex = 1;

	// 当前是否处于可衔接窗口内（由 AN_AttackComboWindow 的开/关事件切换）
	bool bComboWindowOpen = false;

	// 正在主动切段：抑制上一段 Montage 的 Interrupted 回调
	bool bTransitioning = false;

	// 已进入落地段：落地检测只触发一次，落地段结束后才结束 GA
	bool bInLanding = false;

	// 正在交接第三段给 GA_AirAttack：防止同一帧的重复输入触发两次交接
	bool bHandingOff = false;

	// 当前在播的段
	UPROPERTY()
	UAnimMontage* CurrentPlayingMontage = nullptr;

	FTimerHandle LandCheckTimerHandle;

	// 播放第 InIndex
	void PlayStage(int32 InIndex);

	// 窗口内输入：推进到下一段
	void AdvanceToNextStage();

	// 第三段交接：结束本 GA，并安排在下一帧触发常驻的 GA_AirAttack 接管下砸
	void HandoffToDiveAttack();

	// 交接的实际触发。延迟到下一帧：在 GA 结束的调用栈里再激活另一个 GA 属于重入，GAS 对此敏感
	UFUNCTION()
	void TriggerDiveHandoff();

	// 挂上剑气事件监听（Montage 的挥刀帧放 AN 发送）
	void SetupSwordSlashListener();

	// 剑气事件回调：生成一道剑气并斩出
	UFUNCTION()
	void HandleSwordQiRequest(FGameplayEventData EventData);

	// 在剑的 Socket 上生成一道剑气；方向优先朝锁定目标，无锁定回退角色正前方
	void SpawnSwordQi();

	// 停掉当前段（调用前先置 bTransitioning）
	void StopCurrentPlayingMontage();

	UAnimMontage* GetMontageForStage(int32 InIndex) const;

	// 进落地段：停掉空中段、改播 LandMontage
	void EnterLandPhase();

	// 清掉落地检测（LandedDelegate + 轮询 Timer），进入落地段或结束 GA 时调用
	void ClearLandDetection();

	// 覆写：空中 / 落地段被移动打断时，停掉当前在播的那一段
	virtual UAnimMontage* GetActiveMontageForCancel() const override { return CurrentPlayingMontage; }

	// 进窗 
	UFUNCTION()
	void OnComboWindowBegin(FGameplayEventData Payload);
	
	//出窗
	UFUNCTION()
	void OnComboWindowEnd(FGameplayEventData Payload);

	// 输入：仅在窗口内、且未打满三段时推进
	UFUNCTION()
	void OnLightAttackInput(FGameplayEventData Payload);

	// 本段自然播完（窗口内不输入）→ 结束 GA
	UFUNCTION()
	void OnStageMontageCompleted();

	// 本段被外部打断（如闪避抢占同 slot）→ 结束 GA；主动切段时 bTransitioning 已置位，直接忽略
	UFUNCTION()
	void OnStageMontageInterrupted();

	// ── 落地 ──────────────────────────────────────────────
	UFUNCTION()
	void OnLanded(const FHitResult& Hit);

	UFUNCTION()
	void PollLandCheck();

	// 落地段播完 / 被打断 → 结束 GA
	UFUNCTION()
	void OnLandMontageFinished();

	// ── 居合窗口 ──────────────────────────────────────────
	// 每段出手时开启：给 ASC 挂 State.JuheReady，JuheReadyWindow 秒后清除，
	// 表示「刚打完普攻，此刻闪避可进居合」。与地面普攻（GA_Combo_Phase_2）同理。
	// 窗口跨本 GA 结束继续计时，因此 EndAbility 不清理。
	void OpenJuheReadyWindow();

	// 每段出手后允许触发居合的窗口时长（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Juhe")
	float JuheReadyWindow = 3.f;

	UFUNCTION()
	void ClearJuheReady();

	FTimerHandle JuheReadyTimer;

	// 窗口要跨本 GA 结束继续计时，缓存 ASC 以免到期时拿不到 ActorInfo
	TWeakObjectPtr<UAbilitySystemComponent> JuheReadyASC;
};