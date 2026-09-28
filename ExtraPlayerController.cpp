 #include "ExtraPlayerController.h"
#include "ExtraCharacter.h"
#include "GAS/ExtraAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "ExtraPlayerCharacter.h"
#include "HAL/PlatformTime.h"


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

void AExtraPlayerController::AcknowledgePossession(class APawn* P)
 {
	 Super::AcknowledgePossession(P);
	
	AExtraPlayerCharacter* PC = Cast<AExtraPlayerCharacter>(P);
 	if (PC)
 	{
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

bool AExtraPlayerController::InputKey(const FInputKeyEventArgs& EventArgs)
{
	// 键盘自动重复会重复投递按下事件；已记录的键不刷新起点，否则按住时长会被越算越短
	if (EventArgs.Event == IE_Pressed)
	{
		if (!KeyPressTimes.Contains(EventArgs.Key))
		{
			KeyPressTimes.Add(EventArgs.Key, FPlatformTime::Seconds());
		}
	}
	else if (EventArgs.Event == IE_Released)
	{
		if (const double* PressedAt = KeyPressTimes.Find(EventArgs.Key))
		{
			const double HeldTime = FPlatformTime::Seconds() - *PressedAt;
			KeyPressTimes.Remove(EventArgs.Key);

			// 阈值取自角色的重击长按判定，保证日志口径与实际重击判定一致
			if (const AExtraPlayerCharacter* PlayerCharacter = GetPawn<AExtraPlayerCharacter>())
			{
				const bool bLongPress = HeldTime >= PlayerCharacter->GetHeavyAttackHoldTime();
				const FString Message = FString::Printf(TEXT("[Input] %s %s %.3fs"),
					*EventArgs.Key.GetFName().ToString(),
					bLongPress ? TEXT("长按") : TEXT("点按"),
					HeldTime);

				GEngine->AddOnScreenDebugMessage(-1, 4.f, bLongPress ? FColor::Orange : FColor::Green, Message);
				UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
			}
		}
	}

	return Super::InputKey(EventArgs);
}
