#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"
#include "ANS_CombatCamera.generated.h"

/**
 * 战斗相机 Notify State：在 Montage 时间线上标注一段「相机意图」区间。
 *
 * Notify Begin 把 CameraRequest推入角色的CombatCameraComponent请求栈
 * Notify End 移除Request。真正的相机解算与写入由UCombatCameraComponent完成。
 * 可选的 RequiredOwnerTag 用于「同一段动画里按运行时条件决定是否切镜头」：
 */
UCLASS(meta = (DisplayName = "Combat Camera"))
class EXTRACTGAMECHARACTER_API UANS_CombatCamera : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 该区间要提交的相机意图。
	UPROPERTY(EditAnywhere, Category = "CombatCamera")
	FCombatCameraRequest CameraRequest;

	// 条件 Tag：留空表示无条件提交；填了则要求 Owner 的 ASC 拥有该 Tag 才会提交请求。
	// 条件可能在 NotifyBegin 之后才满足（例如穿透 Tag 由 MotionWarping 区间在动画更新之后挂上），
	// 因此未满足时不立即放弃，而是在区间内每帧复核，一旦满足就补提交。
	UPROPERTY(EditAnywhere, Category = "CombatCamera", meta = (Categories = "State"))
	FGameplayTag RequiredOwnerTag;

private:
	// Owner 是否持有条件 Tag（未配置条件时恒为 true）
	bool CheckRequiredTag(USkeletalMeshComponent* MeshComp) const;

	// 在 Owner 的 CombatCameraComponent 上提交请求
	void PushCameraRequest(USkeletalMeshComponent* MeshComp);

	// 撤销该 mesh 已登记的请求（若有）。NotifyEnd 与「区间重播」两条路径共用。
	void PopRequestForMesh(USkeletalMeshComponent* MeshComp);

	// 按 mesh 记录各自推入的请求 ID / 待复核状态。
	// UAnimNotifyState 是动画资产上的共享实例：同一动画被多个 mesh 同时播放（玩家与敌人共用
	// 攻击动画）时，单一成员会被互相覆盖，导致 NotifyEnd 漏 Pop、请求滞留在相机组件里。
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, int32> CachedRequestIds;
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> PendingMeshComps;
};
