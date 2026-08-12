
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ExtraPlayerController.generated.h"

class UExtraAbilitySystemComponent;
class AExtraCharacter;

/**
 * 玩家控制器
 * 在 Possess 时触发角色的 GAS 初始化流程。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
};
