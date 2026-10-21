#include "ExtraPlayerController.h"
#include "ExtraCharacter.h"
#include "UI/GameplayWidget.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "ExtraPlayerCharacter.h"


 void AExtraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AExtraCharacter* Own = Cast<AExtraCharacter>(InPawn);
	if (Own)
	{
		Own->ServerSideInit();
	}
}

void AExtraPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);

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

