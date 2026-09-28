
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/GameplayWidget.h"
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
	
	virtual void AcknowledgePossession(class APawn* P) override;

	// 输入调试打印：松开时按角色的重击长按阈值区分长按/点按
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;

	UPROPERTY()
	UGameplayWidget* GameplayWidget;

	UPROPERTY(EditDefaultsOnly,Category="UI")
	TSubclassOf<class UGameplayWidget> GameplayWidgetClass;

	//在本地Player的视口内渲染UI
	void SpawnGameplayWidget();

private:
	// 各按键的按下时刻，用于松开时计算按住时长
	TMap<FKey, double> KeyPressTimes;
};
