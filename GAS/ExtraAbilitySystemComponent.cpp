#include "ExtraAbilitySystemComponent.h"
#include "ExtraGameplayAbility.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "Engine/Engine.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

namespace
{
	// Skill_02 充能调试的固定屏幕 Key（同 Key 覆盖上一条，不刷屏）
	constexpr uint64 Skill02ChargeDebugKey = 0x5A120001;
	constexpr uint64 Skill02RecoverDebugKey = 0x5A120002;

	// 信息留存时长：靠这个时长常驻，而不是每帧重画
	constexpr float Skill02DebugDuration = 600.f;

	// 未回满时的刷新间隔（只在有层正在回充时运行，回满即停）
	constexpr float Skill02DebugRefreshInterval = 0.1f;
}

void UExtraAbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();

	SetupSkill02ChargeDebug();
}

void UExtraAbilitySystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Skill02ChargeTagEventHandle.IsValid())
	{
		RegisterGameplayTagEvent(UUExtraAbilitySystemStatic::GetSkill02CooldownTag(), EGameplayTagEventType::AnyCountChange)
			.Remove(Skill02ChargeTagEventHandle);
		Skill02ChargeTagEventHandle.Reset();
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(Skill02ChargeRefreshTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void UExtraAbilitySystemComponent::SetupSkill02ChargeDebug()
{
	if (!bShowSkill02ChargeDebug)
	{
		return;
	}
	
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}
	
	// 注册即回调一次，顺带完成初始显示
	Skill02ChargeTagEventHandle = RegisterAndCallGameplayTagEvent(
		UUExtraAbilitySystemStatic::GetSkill02CooldownTag(),
		FOnGameplayEffectTagCountChanged::FDelegate::CreateUObject(this, &UExtraAbilitySystemComponent::OnSkill02ChargeTagChanged),
		EGameplayTagEventType::AnyCountChange);
}

void UExtraAbilitySystemComponent::OnSkill02ChargeTagChanged(FGameplayTag Tag, int32 NewCount)
{
	RefreshSkill02ChargeDebug();

	// 有层正在回充 → 保持刷新（倒计时递减）；回满 → 停掉，最后一条靠 600s 时长留存。
	// 注意 Cooldown GE 存在期间它的 granted tag 计数恒为 1，所以 1→2 层这种计数不变的
	// 变化只能靠这个计时器兜住。
	if (NewCount > 0)
	{
		if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(Skill02ChargeRefreshTimer))
		{
			GetWorld()->GetTimerManager().SetTimer(Skill02ChargeRefreshTimer, this,
				&UExtraAbilitySystemComponent::RefreshSkill02ChargeDebug, Skill02DebugRefreshInterval, true);
		}
	}
	else if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(Skill02ChargeRefreshTimer);
	}
}

void UExtraAbilitySystemComponent::RefreshSkill02ChargeDebug()
{
	if (!GEngine)
	{
		return;
	}

	const int32 MaxCharges = UUExtraAbilitySystemStatic::Skill02MaxCharges;
	const FGameplayTagContainer CooldownTags(UUExtraAbilitySystemStatic::GetSkill02CooldownTag());
	const FGameplayEffectQuery CooldownQuery = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CooldownTags);

	// Cooldown GE 的 stack 数 = 已消耗、正在回充的层数
	int32 UsedCharges = 0;
	for (const FActiveGameplayEffectHandle& CooldownHandle : GetActiveEffects(CooldownQuery))
	{
		if (const FActiveGameplayEffect* ActiveCooldown = GetActiveGameplayEffect(CooldownHandle))
		{
			UsedCharges = FMath::Max(UsedCharges, ActiveCooldown->Spec.GetStackCount());
		}
	}
	const int32 Charges = FMath::Clamp(MaxCharges - UsedCharges, 0, MaxCharges);

	GEngine->AddOnScreenDebugMessage(Skill02ChargeDebugKey, Skill02DebugDuration, FColor::Cyan,
		FString::Printf(TEXT("E技能充能：%d/%d"), Charges, MaxCharges));

	if (Charges >= MaxCharges)
	{
		GEngine->RemoveOnScreenDebugMessage(Skill02RecoverDebugKey);
		return;
	}

	float RecoverRemaining = 0.f;
	for (const float Remaining : GetActiveEffectsTimeRemaining(CooldownQuery))
	{
		RecoverRemaining = FMath::Max(RecoverRemaining, Remaining);
	}

	// 测试期 Cooldown GE 的 Duration 填的是极大值（等价于「先不回充」），
	// 那种情况不显示无意义的大数；改回 7s 后这里会自动显示真实倒计时
	const FString RecoverText = RecoverRemaining > 3600.f
		? TEXT("回充已停用（∞）")
		: FString::Printf(TEXT("下次回充：%.1fs"), RecoverRemaining);

	GEngine->AddOnScreenDebugMessage(Skill02RecoverDebugKey, Skill02DebugDuration, FColor::Yellow, RecoverText);
}

void UExtraAbilitySystemComponent::ServerSideInit()
{
	InitializeBaseAttribute();
	ApplyInitialEffects();
	GiveInitialAbilities();
}

void UExtraAbilitySystemComponent::RemoveInnateAbilities()
{
	for (const FGameplayAbilitySpecHandle& Handle : InnateAbilityHandles)
	{
		if (Handle.IsValid())
		{
			ClearAbility(Handle);
		}
	}
	InnateAbilityHandles.Empty();

	for (const FActiveGameplayEffectHandle& Handle : InnateEffectHandles)
	{
		if (Handle.IsValid())
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	InnateEffectHandles.Empty();
}

void UExtraAbilitySystemComponent::ClearCancelWindowHolder(const FGameplayAbilitySpecHandle& InHandle)
{
	if (CancelWindowHolder == InHandle)
	{
		CancelWindowHolder = FGameplayAbilitySpecHandle();
	}
}

void UExtraAbilitySystemComponent::InitializeBaseAttribute()
{
	// 直接注册基类属性集（不再使用 AttributeSetClass 子类方案）
	UExtraGameAttributeSet* AttrSet = NewObject<UExtraGameAttributeSet>(GetOwner());
	if (AttrSet)
	{
		AddAttributeSetSubobject<UExtraGameAttributeSet>(AttrSet);
		InitializeAttributeFromDataTable(AttrSet);
	}
}

void UExtraAbilitySystemComponent::InitializeAttributeFromDataTable(UExtraGameAttributeSet* AttrSet)
{
	if (!AttributeDataTable || !AttrSet)
	{
		return;
	}

	const FExtraCharacterAttributeRow* BestRow = nullptr;
	int32 BestDistance = MAX_int32;

	const UClass* OwnerClass = GetOwner()->GetClass();

	for (const FName& RowName : AttributeDataTable->GetRowNames())
	{
		const FExtraCharacterAttributeRow* Row = AttributeDataTable->FindRow<FExtraCharacterAttributeRow>(RowName, TEXT("AttributeInit"));
		if (!Row || !Row->CharacterClass)
		{
			continue;
		}
		
		int32 Distance = 0;
		const UClass* Cur = OwnerClass;
		while (Cur && Cur != Row->CharacterClass)
		{
			Cur = Cur->GetSuperClass();
			++Distance;
		}

		if (Cur == Row->CharacterClass && Distance < BestDistance)
		{
			BestDistance = Distance;
			BestRow = Row;
		}
	}

	if (!BestRow)
	{
		return;
	}

	AttrSet->SetHealth(BestRow->Health);
	AttrSet->SetMaxHealth(BestRow->MaxHealth);
	AttrSet->SetAttackPower(BestRow->AttackPower);
	AttrSet->SetStamina(BestRow->Stamina);
	AttrSet->SetMaxStamina(BestRow->MaxStamina);
	AttrSet->SetShield(BestRow->Shield);
	AttrSet->SetEnergyMaxValue(BestRow->EnergyMaxValue);
	AttrSet->SetEnergyValue(BestRow->EnergyValue);
}

void UExtraAbilitySystemComponent::ApplyInitialEffects()
{
	FGameplayEffectContextHandle ContextHandle = MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : InitEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingSpec(EffectClass, 1, ContextHandle);
		if (SpecHandle.IsValid())
		{
			FActiveGameplayEffectHandle ActiveHandle = ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			if (ActiveHandle.IsValid())
			{
				InnateEffectHandles.Add(ActiveHandle);
			}
		}
	}
}

void UExtraAbilitySystemComponent::GiveInitialAbilities()
{
	for (const TSubclassOf<UExtraGameplayAbility>& AbilityClass : InnateAbilities)
	{
		if (!AbilityClass)
		{
			continue;
		}
		FGameplayAbilitySpecHandle Handle = GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		InnateAbilityHandles.Add(Handle);
	}
}
