#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ExtraPlayerController.generated.h"

class UExtraAbilitySystemComponent;
class AExtraCharacter;

/**
 * 玩家控制器
 * 在 Possess 时触发角色的 GAS 初始化流程
 * 在每帧输入事件判定结束后处理所有输入
 */
UCLASS()
class EXTRACTGAMECHARACTER_API AExtraPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	//Server端初始化
	virtual void OnPossess(APawn* InPawn) override;
	
	//移除天生GA/GE
	virtual void OnUnPossess() override;
	
	//客户端初始化ASC和本地UI生成
	virtual void AcknowledgePossession(class APawn* P) override;

	
	// 本帧收集到的输入在这里统一处理
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	UPROPERTY()
	class UGameplayWidget* GameplayWidget;

	UPROPERTY(EditDefaultsOnly,Category="UI")
	TSubclassOf<UGameplayWidget> GameplayWidgetClass;

	//在本地Player的视口内渲染UI
	void SpawnGameplayWidget();
};
