#include "ExtraAbilitySystemComponent.h"
#include "ExtraGameplayAbility.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "GameplayEffect.h"
#include "GameFramework/Actor.h"


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

		// 计算当前角色类到该行 CharacterClass 的继承距离（0 = 精确匹配）
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
