// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_FootPlant.h"
#include "ExtractGameCharacter/ExtraGameAnimInstance.h"



void UAN_FootPlant::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	UExtraGameAnimInstance* AnimInst = Cast<UExtraGameAnimInstance>(MeshComp->GetAnimInstance());
	if (AnimInst)
	{
		AnimInst->OnFootPlantNotify(Foot);
	}
}

FString UAN_FootPlant::GetNotifyName_Implementation() const
{
	return Foot == EFootPlant::Left ? TEXT("Foot_L") : TEXT("Foot_R");
}
