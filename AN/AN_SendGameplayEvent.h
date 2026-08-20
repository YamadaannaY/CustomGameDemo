#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_SendGameplayEvent.generated.h"

/**
 *  用于给Anim 提供发送GameplayEventTag的AN标记 
 */
UCLASS()
class UAN_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& EventReference) override;
private:
	//发送给Anim拥有者的Tag以触发GameplayEvent
	UPROPERTY(EditAnywhere,Category="Gameplay Ability")
	FGameplayTag EventTag;

	//将Notify的函数名变为Tag名最后一段
	virtual FString GetNotifyName_Implementation() const override;
};