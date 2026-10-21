#pragma once

#include "CoreMinimal.h"
#include "AudioEventTypes.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_AudioEvent.generated.h"

class UAudioComponent;
class USkeletalMeshComponent;
class USoundBase;

/**
 * 音频 Notify State。
 *
 * Begin 按概率/冷却判定后播放 AudioEvent；Follow 决定是否吸附到 Socket 随动。
 * End 时按 KeepAlive 决定淡出停止还是继续播放，并可衔接 TrailingAudioEvent 尾音。
 */
UCLASS(meta = (DisplayName = "Audio Event"))
class EXTRACTGAMECHARACTER_API UANS_AudioEvent : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

	// 区间开始时播放的主音效
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> AudioEvent;

	// 挂载骨骼 Socket 名，用于定位播放位置
	UPROPERTY(EditAnywhere, Category = "Audio")
	FName SocketName;

	// 是否跟随角色 / Socket 移动
	UPROPERTY(EditAnywhere, Category = "Audio")
	bool bFollow = true;

	// 是否保持存活：区间结束后不淡出，让主音效自然播完
	UPROPERTY(EditAnywhere, Category = "Audio")
	bool bKeepAlive = false;

	// 淡入/淡出时长（秒）
	UPROPERTY(EditAnywhere, Category = "Audio", meta = (ClampMin = "0.0", Units = "s"))
	float FadeDuration = 0.5f;

	// 淡变曲线类型
	UPROPERTY(EditAnywhere, Category = "Audio")
	EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear;

	// 区间结束时衔接播放的收尾音
	UPROPERTY(EditAnywhere, Category = "Audio")
	TObjectPtr<USoundBase> TrailingAudioEvent;

	// 概率 / 冷却控制信息
	UPROPERTY(EditAnywhere, Category = "Audio")
	FAudioEventProbabilityCooldownInfo TagProbabilityInfo;

private:
	struct FResolvedSettings
	{
		FGameplayTag Tag;
		float Probability = 1.f;
		float CooldownTime = 0.f;
	};

	// 用 Owner 当前拥有的 Tag 覆盖默认概率 / 冷却
	FResolvedSettings ResolveSettings(const USkeletalMeshComponent* MeshComp) const;

	// 按 Follow 选择 Attach 或位置播放，并处理淡入
	UAudioComponent* PlaySound(USkeletalMeshComponent* MeshComp, USoundBase* Sound) const;

	// 停掉该 mesh 上一次未正常结束的播放
	void StopForMesh(USkeletalMeshComponent* MeshComp);

	// 清理已失效的弱引用条目
	void PruneStaleEntries();

	// 按 mesh 分桶：UAnimNotifyState 是共享实例，同一动画被多个 mesh 播放时，单一成员会被互相覆盖
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<UAudioComponent>> ActiveAudioComponents;

	// 本次区间是否真正通过概率/冷却并开始播放（决定 End 是否播尾音）
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> StartedMeshComps;
};
