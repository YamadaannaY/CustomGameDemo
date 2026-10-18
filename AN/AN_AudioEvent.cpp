#include "AN_AudioEvent.h"

#include "AudioEventCooldownSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	bool IsDedicatedServer(const USkeletalMeshComponent* MeshComp)
	{
		const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
		return World && World->GetNetMode() == NM_DedicatedServer;
	}

	UAudioEventCooldownSubsystem* GetCooldownSubsystem(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UAudioEventCooldownSubsystem>() : nullptr;
	}
}

void UAN_AudioEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// 声音只在有渲染的一端播放
	if (!MeshComp || !MeshComp->GetOwner() || IsDedicatedServer(MeshComp))
	{
		return;
	}

	//开始找冷却/概率参数
	FGameplayTag Tag;
	float Probability = 1.f;
	float CooldownTime = 0.f;
	ExtraAudioEvent::ResolveProbability(TagProbabilityInfo, MeshComp->GetOwner(), Tag, Probability, CooldownTime);

	if (Probability <= 0.f)
	{
		return;
	}
	if (Probability < 1.f && FMath::FRand() >= Probability)
	{
		return;
	}

	UAudioEventCooldownSubsystem* CooldownSubsystem = GetCooldownSubsystem(MeshComp);
	if (CooldownSubsystem && CooldownSubsystem->IsOnCooldown(MeshComp->GetOwner(), this, Tag, CooldownTime))
	{
		return;
	}

	// 判定全部通过后再加载，避免无谓的同步加载
	USoundBase* Sound = AudioEvent.LoadSynchronous();
	if (!Sound)
	{
		return;
	}

	const bool bHasSocket = !SocketName.IsNone() && MeshComp->DoesSocketExist(SocketName);
	const FName AttachSocket = bHasSocket ? SocketName : NAME_None;

	UAudioComponent* AudioComp = nullptr;
	if (bFollow)
	{
		AudioComp = UGameplayStatics::SpawnSoundAttached(
			Sound, MeshComp, AttachSocket, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, /*bStopWhenAttachedToDestroyed=*/true,
			VolumeMultiplier, PitchMultiplier);
	}
	else
	{
		const FVector Location = bHasSocket ? MeshComp->GetSocketLocation(SocketName) : MeshComp->GetComponentLocation();
		AudioComp = UGameplayStatics::SpawnSoundAtLocation(
			MeshComp->GetWorld(), Sound, Location, FRotator::ZeroRotator,
			VolumeMultiplier, PitchMultiplier);
	}

	if (!AudioComp)
	{
		return;
	}

	if (FadeInDuration > 0.f)
	{
		AudioComp->FadeIn(FadeInDuration, 1.f, 0.f, FadeCurve);
	}

	if (CooldownSubsystem && CooldownTime > 0.f)
	{
		CooldownSubsystem->MarkPlayed(MeshComp->GetOwner(), this, Tag);
	}
}

FString UAN_AudioEvent::GetNotifyName_Implementation() const
{
	return AudioEvent.IsNull() ? TEXT("AudioEvent") : FString::Printf(TEXT("Audio: %s"), *AudioEvent.GetAssetName());
}
