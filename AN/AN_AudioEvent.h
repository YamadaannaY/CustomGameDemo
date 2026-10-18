#pragma once

#include "CoreMinimal.h"
#include "AudioEventTypes.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_AudioEvent.generated.h"

class USkeletalMeshComponent;
class USoundBase;

/**
 * 单点音频 Notify：Notify 触发瞬间播放一次音频。
 */
UCLASS(meta = (DisplayName = "Audio Event"))
class EXTRACTGAMECHARACTER_API UAN_AudioEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 要播放的音频资源（软引用，避免硬加载）
	UPROPERTY(EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundBase> AudioEvent;

	// 音频挂载的骨骼 Socket 名称
	UPROPERTY(EditAnywhere, Category = "Audio")
	FName SocketName;

	// 是否跟随挂点 / 角色播放
	UPROPERTY(EditAnywhere, Category = "Audio")
	bool bFollow = true;

	// 音量倍数
	UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	// 音高倍数
	UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	// 淡入时长（秒）；0 表示不淡入
	UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInDuration = 0.f;

	UPROPERTY(EditAnywhere, Category = "Audio")
	EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear;

	// 概率与冷却控制信息
	UPROPERTY(EditAnywhere, Category = "Audio")
	FAudioEventProbabilityCooldownInfo TagProbabilityInfo;
};
