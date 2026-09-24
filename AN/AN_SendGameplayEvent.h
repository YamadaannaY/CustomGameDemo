#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_SendGameplayEvent.generated.h"

/**
 *  用于给Anim 提供发送GameplayEventTag的AN标记
 *
 *  两种用法：
 *  - 通用：把本类直接放到 Montage 上，在编辑器里填 EventTag。
 *  - 特化：子类覆写 GetEventTag 返回硬编码 Tag（不落序列化）。无需任何构造函数代码，
 *    细节面板自动不再显示 EventTag，杜绝误改。
 */
UCLASS()
class UAN_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& EventReference) override;

	// 本 AN 实际发送的事件 Tag。特化子类覆写返回自己的固定 Tag。
	virtual FGameplayTag GetEventTag() const { return EventTag; }

protected:
	// 子类覆写以给事件附加负载（如 TargetData），在发送事件前调用；默认空载荷。
	virtual void BuildEventData(FGameplayEventData& OutEventData, USkeletalMeshComponent* MeshComp) {}

	//仅通用用法（本类自身）需要：编辑器里指定要发送的任意 Tag。
	//特化子类面板不显示本项（见 CanEditEventTag）。
	UPROPERTY(EditAnywhere,Category="Gameplay Ability",meta=(EditCondition="CanEditEventTag()",EditConditionHides))
	FGameplayTag EventTag;

	//EventTag 是否允许在编辑器里改：只有基类自身（通用用法）需要；
	//特化子类一律由 GetEventTag 硬编码提供，什么都不用做。
	//UFUNCTION：EditCondition 的函数形式按反射查找（返回类型须为 bool）。
	UFUNCTION()
	bool CanEditEventTag() const;

private:
	//将Notify的函数名变为Tag名最后一段
	virtual FString GetNotifyName_Implementation() const override;
};