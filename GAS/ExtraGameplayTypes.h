#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/NetSerialization.h"
#include "ExtraGameplayTypes.generated.h"

// 携带「推力」参数的 TargetData：AN_ApplyPush 通过 GameplayEvent 把推力向量 + 覆盖标志进行发送
USTRUCT()
struct FPushTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	FVector PushVelocity = FVector::ZeroVector;
	bool bOverrideXY = false;
	bool bOverrideZ = true;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << PushVelocity;
		Ar.SerializeBits(&bOverrideXY, 1);
		Ar.SerializeBits(&bOverrideZ, 1);

		bOutSuccess = true;
		return true;
	}
};

template <>
struct TStructOpsTypeTraits<FPushTargetData> : public TStructOpsTypeTraitsBase2<FPushTargetData>
{
	enum { WithNetSerializer = true };
};

// 范围检测的圆心来源
UENUM()
enum class EAreaCenterMode : uint8
{
	// 跟随 GA 上的 bAreaDamageUseLockTargetAsCenter 配置
	Inherit		UMETA(DisplayName = "Inherit From Ability"),
	// 角色位置 + CenterOffset
	Owner		UMETA(DisplayName = "Owner + Offset"),
	// 锁定目标位置；无锁定目标时回退角色位置
	LockTarget	UMETA(DisplayName = "Lock On Target"),
};

// 携带「范围检测」参数的 TargetData：AN_AreaCheck 通过 GameplayEvent 把圆心参数 + 半径发给 GA
USTRUCT()
struct FAreaCheckData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	// 圆心来源。Inherit 时由 GA 的 bAreaDamageUseLockTargetAsCenter 决定
	EAreaCenterMode CenterMode = EAreaCenterMode::Inherit;

	// 圆心相对角色位置的偏移（仅 XY 生效，Z 忽略、沿用角色高度）。LockTarget 模式下忽略
	FVector CenterOffset = FVector::ZeroVector;

	// 检测半径（cm）。<=0 表示沿用 GA 自身配置的 AreaDamageRadius
	float Radius = 0.f;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		uint8 CenterModeValue = static_cast<uint8>(CenterMode);
		Ar << CenterModeValue;
		CenterMode = static_cast<EAreaCenterMode>(CenterModeValue);
		Ar << CenterOffset;
		Ar << Radius;

		bOutSuccess = true;
		return true;
	}
};

template <>
struct TStructOpsTypeTraits<FAreaCheckData> : public TStructOpsTypeTraitsBase2<FAreaCheckData>
{
	enum { WithNetSerializer = true };
};

// 伤害窗口标识：随命中事件携带，GA 用它区分同一个 Montage 里的多个伤害区间
USTRUCT()
struct FDamageWindowData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()

	FName WindowId = NAME_None;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << WindowId;
		bOutSuccess = true;
		return true;
	}
};

template <>
struct TStructOpsTypeTraits<FDamageWindowData> : public TStructOpsTypeTraitsBase2<FDamageWindowData>
{
	enum { WithNetSerializer = true };
};

// 一段伤害：攻击力 × Coefficient，重复 Count 次
USTRUCT(BlueprintType)
struct FDamageSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float Coefficient = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	int32 Count = 1;

	// 这一段触发击飞（击飞速度见下方 KnockbackVelocity）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bKnockDown = false;

	// 击飞速度（攻击者本地空间：X=前、Y=右、Z=上），仅 bKnockDown 时生效
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (EditCondition = "bKnockDown"))
	FVector KnockbackVelocity = FVector(0.0f, 0.0f, 600.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (EditCondition = "bKnockDown"))
	bool bOverrideXY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (EditCondition = "bKnockDown"))
	bool bOverrideZ = true;
};

// 单个伤害窗口的配置：WindowId 与 AN/ANS 上填的一致
USTRUCT(BlueprintType)
struct FDamageWindowConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	FName WindowId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TArray<FDamageSegment> DamageSegments;
};

// 一段攻击（Montage 的一个 Section）的完整配置：伤害段 + 能量 + 表现
USTRUCT(BlueprintType)
struct FAttackSegmentConfig
{
	GENERATED_BODY()

	// 兜底：未匹配到具体伤害窗口时使用
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TArray<FDamageSegment> DamageSegments;

	// 按伤害窗口细分：同一个 Montage 里多个伤害区间各用各的段
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TArray<FDamageWindowConfig> DamageWindows;

	// 相邻伤害段的间隔（秒）；0 = 同帧全部结算
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float SegmentInterval = 0.08f;

	// 本段命中给的能量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.0"))
	float EnergyGain = 0.f;

	// 是否发受击表现：多段时只在第一段和击飞段发一次
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bSendHitReact = true;
};
