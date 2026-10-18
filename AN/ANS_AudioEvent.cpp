#include "ANS_AudioEvent.h"

#include "AudioEventCooldownSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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

UANS_AudioEvent::FResolvedSettings UANS_AudioEvent::ResolveSettings(const USkeletalMeshComponent* MeshComp) const
{
	FResolvedSettings Settings;
	ExtraAudioEvent::ResolveProbability(TagProbabilityInfo, MeshComp ? MeshComp->GetOwner() : nullptr, Settings.Tag, Settings.Probability, Settings.CooldownTime);
	return Settings;
}

UAudioComponent* UANS_AudioEvent::PlaySound(USkeletalMeshComponent* MeshComp, USoundBase* Sound) const
{
	if (!MeshComp || !Sound)
	{
		return nullptr;
	}

	const bool bHasSocket = !SocketName.IsNone() && MeshComp->DoesSocketExist(SocketName);
	const FName AttachSocket = bHasSocket ? SocketName : NAME_None;
	const float FadeInSeconds = FMath::Max(0.f, FadeDuration);

	UAudioComponent* AudioComp = nullptr;
	if (bFollow)
	{
		AudioComp = UGameplayStatics::SpawnSoundAttached(
			Sound, MeshComp, AttachSocket, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, /*bStopWhenAttachedToDestroyed=*/true);
	}
	else
	{
		const FVector Location = bHasSocket ? MeshComp->GetSocketLocation(SocketName) : MeshComp->GetComponentLocation();
		AudioComp = UGameplayStatics::SpawnSoundAtLocation(
			MeshComp->GetWorld(), Sound, Location, FRotator::ZeroRotator);
	}

	if (AudioComp && FadeInSeconds > 0.f)
	{
		AudioComp->FadeIn(FadeInSeconds, 1.f, 0.f, FadeCurve);
	}

	return AudioComp;
}

void UANS_AudioEvent::StopForMesh(USkeletalMeshComponent* MeshComp)
{
	StartedMeshComps.Remove(MeshComp);

	TWeakObjectPtr<UAudioComponent> AudioComp;
	if (ActiveAudioComponents.RemoveAndCopyValue(MeshComp, AudioComp) && AudioComp.IsValid())
	{
		AudioComp->Stop();
	}
}

void UANS_AudioEvent::PruneStaleEntries()
{
	for (auto It = ActiveAudioComponents.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = StartedMeshComps.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UANS_AudioEvent::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 声音只在有渲染的一端播放
	if (!MeshComp || !MeshComp->GetOwner() || !AudioEvent || IsDedicatedServer(MeshComp))
	{
		return;
	}

	PruneStaleEntries();

	// 上一次区间被打断后重播，先停掉旧声音，避免叠加
	StopForMesh(MeshComp);

	const FResolvedSettings Settings = ResolveSettings(MeshComp);
	if (Settings.Probability <= 0.f)
	{
		return;
	}
	if (Settings.Probability < 1.f && FMath::FRand() >= Settings.Probability)
	{
		return;
	}

	UAudioEventCooldownSubsystem* CooldownSubsystem = GetCooldownSubsystem(MeshComp);
	if (CooldownSubsystem && CooldownSubsystem->IsOnCooldown(MeshComp->GetOwner(), this, Settings.Tag, Settings.CooldownTime))
	{
		return;
	}

	UAudioComponent* AudioComp = PlaySound(MeshComp, AudioEvent);
	if (!AudioComp)
	{
		return;
	}

	ActiveAudioComponents.Add(MeshComp, AudioComp);
	StartedMeshComps.Add(MeshComp);

	if (CooldownSubsystem && Settings.CooldownTime > 0.f)
	{
		CooldownSubsystem->MarkPlayed(MeshComp->GetOwner(), this, Settings.Tag);
	}
}

void UANS_AudioEvent::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// 未真正开始播放（概率/冷却跳过）时，不播尾音
	if (StartedMeshComps.Remove(MeshComp) == 0)
	{
		ActiveAudioComponents.Remove(MeshComp);
		return;
	}

	if (TrailingAudioEvent && !IsDedicatedServer(MeshComp))
	{
		PlaySound(MeshComp, TrailingAudioEvent);
	}

	TWeakObjectPtr<UAudioComponent> AudioComp;
	const bool bHadComponent = ActiveAudioComponents.RemoveAndCopyValue(MeshComp, AudioComp);
	if (bKeepAlive || !bHadComponent || !AudioComp.IsValid())
	{
		return;
	}

	const float FadeOutSeconds = FMath::Max(0.f, FadeDuration);
	if (FadeOutSeconds > 0.f)
	{
		AudioComp->FadeOut(FadeOutSeconds, 0.f, FadeCurve);
	}
	else
	{
		AudioComp->Stop();
	}
}

FString UANS_AudioEvent::GetNotifyName_Implementation() const
{
	return AudioEvent ? FString::Printf(TEXT("Audio: %s"), *AudioEvent->GetName()) : TEXT("AudioEvent");
}
