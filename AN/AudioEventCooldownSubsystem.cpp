#include "AudioEventCooldownSubsystem.h"

#include "Engine/World.h"

bool UAudioEventCooldownSubsystem::IsOnCooldown(const UObject* Owner, const UObject* Source, FGameplayTag Key, float CooldownSeconds) const
{
	if (!Owner || !Source || CooldownSeconds <= 0.f)
	{
		return false;
	}

	const FCooldownRecord* Record = Records.Find(FAudioEventCooldownKey{ FObjectKey(Owner), FObjectKey(Source), Key });
	if (!Record)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	return (Now - Record->LastPlayTime) < CooldownSeconds;
}

void UAudioEventCooldownSubsystem::MarkPlayed(const UObject* Owner, const UObject* Source, FGameplayTag Key)
{
	if (!Owner || !Source)
	{
		return;
	}

	// 惰性清理：FObjectKey 不随对象销毁自动失效
	if (Records.Num() >= 64)
	{
		PruneInvalidOwners();
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	Records.FindOrAdd(FAudioEventCooldownKey{ FObjectKey(Owner), FObjectKey(Source), Key }).LastPlayTime = Now;
}

void UAudioEventCooldownSubsystem::PruneInvalidOwners()
{
	for (auto It = Records.CreateIterator(); It; ++It)
	{
		if (!It.Key().Owner.ResolveObjectPtr())
		{
			It.RemoveCurrent();
		}
	}
}
