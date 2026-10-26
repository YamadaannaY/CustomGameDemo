#include "ExtraCurveCameraShake.h"

#include "ExtraCurveCameraShakePattern.h"

UExtraCurveCameraShake::UExtraCurveCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UExtraCurveCameraShakePattern>(TEXT("RootShakePattern")))
{
}
