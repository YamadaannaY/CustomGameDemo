#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AudioEventTypes.generated.h"

class AActor;

/**
 * 一个ProbabilityInfo包含一个Tag和它对应的播放概率、触发间隔
 * 触发一个Audio时会先判断ASC中是否持有此Tag，如果有对这个Audio应用对应参数
 */
USTRUCT(BlueprintType)
struct FAudioEventProbabilityInfo
{
	GENERATED_BODY()

	// 按数组顺序取第一个 Owner 拥有的 Tag
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "State"))
	FGameplayTag Tag;

	// 播放概率 [0,1]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Probability = 1.f;

	// 冷却时长（秒）：距该 Owner 上次成功播放不足该值时跳过
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", Units = "s"))
	float CooldownTime = 0.f;
};

/**
 * 音频播放的概率 / 冷却配置。
 */
USTRUCT(BlueprintType)
struct FAudioEventProbabilityCooldownInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultProbability = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", Units = "s"))
	float DefaultCooldownTime = 0.f;

	//一个Audio参与判断的所有Tag及其对应概率/冷却的数组，数组中Info的顺序代表着优先级
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FAudioEventProbabilityInfo> TagProbability;
};

namespace ExtraAudioEvent
{
	// 解析生效的概率/冷却：Owner 拥有的首个 Tag 覆盖默认值
	void ResolveProbability(const FAudioEventProbabilityCooldownInfo& Info,AActor* Owner, FGameplayTag& OutTag, float& OutProbability, float& OutCooldown);
}
