#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_GameplayEventWindow.generated.h"

/**
 * 通用「区间事件窗口」：在 AN 覆盖的区间首尾各广播一次 GameplayEvent。
 *
 * 两种用法（与 UAN_SendGameplayEvent 一致）：
 *  - 通用：把本类直接放到 Montage 上，编辑器里分别填「进窗 / 出窗」两个 Tag。
 *  - 特化：子类覆写 GetWindowBeginTag / GetWindowEndTag 返回硬编码 Tag，
 *    细节面板自动不再显示这两个字段，杜绝误改。
 *
 * 两个 Tag 都为空则不发送任何事件。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAN_GameplayEventWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	// 本 AN 在窗口首 / 尾实际广播的事件 Tag。特化子类覆写返回自己的固定 Tag。
	virtual FGameplayTag GetWindowBeginTag() const { return WindowBeginTag; }
	virtual FGameplayTag GetWindowEndTag() const { return WindowEndTag; }

protected:
	UPROPERTY(EditAnywhere, Category = "Gameplay Ability", meta = (EditCondition = "CanEditWindowTags()", EditConditionHides))
	FGameplayTag WindowBeginTag;

	UPROPERTY(EditAnywhere, Category = "Gameplay Ability", meta = (EditCondition = "CanEditWindowTags()", EditConditionHides))
	FGameplayTag WindowEndTag;

	// 两个 Tag 是否允许在编辑器里改：只有基类自身（通用用法）需要；
	// 特化子类一律由覆写的 GetWindowBeginTag / GetWindowEndTag 提供。
	// 必须是 UFUNCTION：EditCondition 的函数形式按反射查找（返回类型须为 bool）。
	UFUNCTION()
	bool CanEditWindowTags() const;

	// 按 Tag 广播一次无负载事件（Tag 无效 / 无 ASC 时跳过）
	void SendWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag) const;
};
