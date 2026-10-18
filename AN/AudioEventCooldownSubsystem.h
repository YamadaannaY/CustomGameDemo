#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/ObjectKey.h"
#include "AudioEventCooldownSubsystem.generated.h"

/**
 * 冷却记录的键：角色 + 音效通知对象 + 命中 Tag。
 * 带上通知对象，避免不同音效共用同一条冷却记录而互相压制。
 * 
 */
struct FAudioEventCooldownKey
{
	FObjectKey Owner;
	FObjectKey Source;
	
	//同一个AN/ANS下根据不同Tag采用不同的CooldownTime
	FGameplayTag Tag;

	bool operator==(const FAudioEventCooldownKey& Other) const
	{
		return Owner == Other.Owner && Source == Other.Source && Tag == Other.Tag;
	}

	//哈希函数，提供哈希对比能力
	friend uint32 GetTypeHash(const FAudioEventCooldownKey& Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.Owner), GetTypeHash(Key.Source)), GetTypeHash(Key.Tag));
	}
};

/**
 * 音频通知的冷却记录。
 * 按 Owner + 通知对象 + 生效 Tag 记录上次成功播放的世界时间，冷却不跨角色共享。
 * 冷却记录是运行时状态，取决于游戏进程而不是某个Level，所以用跟随GI的SubSystem保证跨Level保留。使用WorldSubSystem切换关卡System会被销毁
 * 任何对象都可以通过找到当前的GI而获取此SubSystem，对于资产级的共享对象AN/ANS进行直接操控的AudioEvent而言，角色本身并不合适(多端同角色实例不同)
 * 创建一个无关对象的GI级系统最好，并且对于上述情况，每个端各自拥有自己的AudioSubSys，互不污染。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UAudioEventCooldownSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 距上次成功播放是否仍未满冷却；无记录时视为可用
	bool IsOnCooldown(const UObject* Owner, const UObject* Source, FGameplayTag Key, float CooldownSeconds) const;

	// 记录一次成功播放
	void MarkPlayed(const UObject* Owner, const UObject* Source, FGameplayTag Key);

private:
	struct FCooldownRecord
	{
		//播放时记录的世界时间
		double LastPlayTime = -MAX_dbl;
	};

	void PruneInvalidOwners();

	TMap<FAudioEventCooldownKey, FCooldownRecord> Records;
};
