#include "ExtraPlayerController.h"
#include "ExtraCharacter.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "ExtraPlayerCharacter.h"


 void AExtraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AExtraCharacter* PC = Cast<AExtraCharacter>(InPawn);
	if (PC)
	{
		PC->ServerSideInit();
	}
}

void AExtraPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);

	// 本帧收集到的输入在这里统一处理：未激活的能力尝试激活（LocalPredicted 下会连带
	// ServerTryActivateAbility 让服务端也激活），已激活的把输入事件喂给它——后者走 GAS 自带的
	// 输入复制通道，服务端已激活的 GA 用 WaitInputPress 就能收到同一次输入
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetPawn()))
	{
		if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(ASI->GetAbilitySystemComponent()))
		{
			ASC->ProcessAbilityInput();
		}
	}
}

void AExtraPlayerController::OnUnPossess()
{
	if (APawn* CurrentPawn = GetPawn())
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(CurrentPawn))
		{
			if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(ASI->GetAbilitySystemComponent()))
			{
				ASC->RemoveInnateAbilities();
			}
		}
	}

	Super::OnUnPossess();
}

void AExtraPlayerController::AcknowledgePossession(class APawn* P)
 {
	 Super::AcknowledgePossession(P);
	
	AExtraPlayerCharacter* PC = Cast<AExtraPlayerCharacter>(P);
 	if (PC)
 	{
 		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PC))
 		{
 			if (UExtraAbilitySystemComponent* ASC = Cast<UExtraAbilitySystemComponent>(ASI->GetAbilitySystemComponent()))
 			{
 				ASC->InitAbilityActorInfo(PC,PC);
 			}
 		}
		//在客户端渲染
		SpawnGameplayWidget();
 	}
 }

void AExtraPlayerController::SpawnGameplayWidget()
 {
	if (!IsLocalPlayerController()) return;

	//本地Player拥有的视口UI
	GameplayWidget=CreateWidget<UGameplayWidget>(this,GameplayWidgetClass);
	if (GameplayWidget)
	{
		GameplayWidget->AddToViewport();
	}
 }

