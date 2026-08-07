#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_FootPlant.generated.h"

/**
 *  记录落脚点，被触发时更新AnimInst中的Foot标记 
 */
UENUM(BlueprintType)
enum class EFootPlant : uint8
{
	None  UMETA(DisplayName = "None"),
	Left  UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

UCLASS()
class UAN_FootPlant : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, Category = "Foot Plant")
	EFootPlant Foot = EFootPlant::Left;
};