#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UExtraAbilitySystemStatic.generated.h"

/**
 * 
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UUExtraAbilitySystemStatic : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	static FGameplayTag GetBasicAttackAbilityTag();
};
