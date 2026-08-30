#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_WeaponTrace.generated.h"

/**
 * 武器轨迹扫描窗口：在 Montage 时间线上标注一段「碰撞生效」区间。
 *
 * NotifyBegin → WeaponComponent.BeginWeaponTrace()（收集当前武器组的 TraceSocket 并清空窗口状态）
 * NotifyTick  → 每动画帧推进一次 prev→cur 球体扫描
 * NotifyEnd   → 关闭窗口，把本窗口命中集合经 GameplayEvent 发送给 Owner（由 GA 处理伤害）
 */
UCLASS(meta = (DisplayName = "Weapon Trace"))
class EXTRACTGAMECHARACTER_API UANS_WeaponTrace : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
