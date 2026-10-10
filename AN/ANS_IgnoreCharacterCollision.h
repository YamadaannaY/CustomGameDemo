#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Engine/EngineTypes.h"
#include "ANS_IgnoreCharacterCollision.generated.h"

class AExtraCharacter;
class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;

/**
 * 穿透碰撞 Notify State：区间内让 Owner 与场上其他 AExtraCharacter 互相忽略移动抓取，
 * 使带位移的动画（如居合前冲的穿身、绕圈回原点的斩击）能穿过对方的胶囊而不被挡住。
 *
 * 必须双向：只让发起者忽略对手的话，对手自己的移动扫掠仍会撞上发起者并做穿透解算；
 * 两个竖直胶囊接近同心时最短分离方向会落到竖直方向，表现为把对手顶起来（随后掉落播落地动画）。
 *
 * 另外只忽略「移动抓取」不够：绕圈位移的半径小于两胶囊半径之和时，重叠本身就是必然状态，
 * 而 CMC 的角色间斥力（bEnablePhysicsInteraction）只看 overlap、不查忽略列表，
 * 会每帧把双方顶开。因此区间内按次关掉双方斥力，退出时按原值恢复。
 *
 * 状态一律按「角色」分桶存放，而不是放在单个成员槽位上：本对象是蒙太奇资产的 Instanced 子对象，
 * 同一份资产在 server 与每个 client 各播一次、共用的是同一个实例；单槽位时后进的一端会把先前的
 * 记录顶掉，出窗时就只有最后一端能恢复碰撞，其余永久穿透。
 *
 * 收尾不只看 NotifyEnd：蒙太奇被 EndGA 提前停播（惯性化淡出会把 BlendTime 强制成 0）时
 * NotifyEnd 不保证到达，所以每个窗口另挂一个自续期定时器，以「承载区间的蒙太奇是否还在播」
 * 为准兜底收窗。
 *
 * 只管碰撞忽略——位移与朝向仍由 GA / MotionWarping 的区间负责，两者互不干扰。
 */
UCLASS(meta = (DisplayName = "Ignore Character Collision"))
class EXTRACTGAMECHARACTER_API UANS_IgnoreCharacterCollision : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

protected:
	// 一次穿透窗口（key 是发起该窗口的角色）
	struct FWindowRecord
	{
		// 承载该区间的蒙太奇：停播即说明 NotifyEnd 丢了，兜底据此收窗
		TWeakObjectPtr<UAnimMontage> Source;

		// 承载该区间的网格，用于取 AnimInstance
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;

		// 本窗口拉入过的对手，收窗时按此注销
		TArray<TWeakObjectPtr<AExtraCharacter>> PulledPeers;

		// 兜底自检用的定时器（单次 + 自续期，角色销毁后自然停）
		FTimerHandle WatchdogHandle;
	};

	void OpenWindow(AExtraCharacter* OwnerChar, USkeletalMeshComponent* MeshComp, UAnimMontage* Source);
	void CloseWindow(AExtraCharacter* OwnerChar);

	// 按「自己有没有窗口 / 被谁拉入」重算某角色该忽略谁、斥力该不该关；幂等，差量更新
	void RebuildStateFor(AExtraCharacter* Char);

	// 以「承载区间的蒙太奇是否还在播」为准的兜底自检
	void ScheduleWatchdog(AExtraCharacter* OwnerChar);
	void OnWindowWatchdog(TWeakObjectPtr<AExtraCharacter> OwnerChar);

	// 惰性剔除已销毁角色留下的失效键（被销毁的一方没有机会再清自己）
	void PruneInvalidEntries();

	// 发起窗口的角色 → 窗口记录
	TMap<TWeakObjectPtr<AExtraCharacter>, FWindowRecord> Windows;

	// 被拉入者 → 拉它入穿透的角色（去重；决定它要忽略谁、斥力要不要关）
	TMap<TWeakObjectPtr<AExtraCharacter>, TArray<TWeakObjectPtr<AExtraCharacter>>> Pullers;

	// 角色 → 当前实际写进它移动忽略列表的对手（差量更新用）
	TMap<TWeakObjectPtr<AExtraCharacter>, TArray<TWeakObjectPtr<AActor>>> AppliedIgnores;

	// 被我们关过斥力的角色 → 原值；有键即表示当前是关着的
	TMap<TWeakObjectPtr<AExtraCharacter>, bool> PhysicsBackups;
};
