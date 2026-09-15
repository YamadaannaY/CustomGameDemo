#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_IgnoreCharacterCollision.generated.h"

class ACharacter;

/**
 * 穿透碰撞 Notify State：区间内让 Owner 的胶囊在移动时忽略场上所有 AExtraCharacter，
 * 使带位移的动画（如居合前冲的穿身）能穿过其他角色的胶囊而不被挡住。
 *
 * 只管碰撞忽略——位移与朝向仍由 GA / MotionWarping 的 AttackFacing 区间负责，两者互不干扰。
 *
 * NotifyEnd 恢复碰撞；但蒙太奇被强行打断时 NotifyEnd 不保证到达，
 * 因此另外挂了 OnMontageBlendingOut 做兜底清理，避免碰撞忽略残留。
 */
UCLASS(meta = (DisplayName = "Ignore Character Collision"))
class EXTRACTGAMECHARACTER_API UANS_IgnoreCharacterCollision : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

private:
	// 收集场上所有 ExtraCharacter 并加入 Owner 的移动忽略列表
	void ApplyIgnore();

	// 恢复碰撞并清空记录（幂等，可重复调用）
	void ClearIgnore();

	// 兜底：蒙太奇被打断 / 强行 BlendOut 时 NotifyEnd 不保证到达
	UFUNCTION()
	void OnOwnerMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	// 本次忽略的角色（弱引用，期间被销毁的自动失效）
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;

	// Owner 角色与动画实例，供兜底清理与解绑使用
	TWeakObjectPtr<ACharacter> OwnerCharacter;
	TWeakObjectPtr<UAnimInstance> BoundAnimInstance;
};
