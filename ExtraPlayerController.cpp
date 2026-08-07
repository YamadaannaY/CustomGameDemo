#include "ExtraPlayerController.h"
#include "ExtraCharacter.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"


void AExtraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AExtraCharacter* PC = Cast<AExtraCharacter>(InPawn);
	if (PC)
	{
		PC->ServerSideInit();
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
