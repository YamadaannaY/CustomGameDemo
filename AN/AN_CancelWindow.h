#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_CancelWindow.generated.h"

/**
 * 后摇「可打断窗口」标记。
 *
 * 覆盖在 Montage 后摇段，语义为：进入窗口后该 GA 视为已取消——期间
 *  1) 任何其他 GA 激活打断（NotifyBegin 发 ability.cancelwindow.begin，持有者撤销自身封锁并登记，
 *     其他 GA 在 CommitAbility 时取消它）；
 *  2) 移动输入打断（NotifyTick 检测到移动输入即发 ability.cancel）。
 * NotifyEnd 发 ability.cancelwindow.end，持有者恢复封锁。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_CancelWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
