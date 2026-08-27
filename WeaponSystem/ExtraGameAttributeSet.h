// Copyright Yu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "ExtraGameAttributeSet.generated.h"

class AActor;

// 属性访问宏（简写）
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * 角色基础属性集
 * 武器 GE 可以修改 AttackPower 等属性来实现武器间的数值差异。
 */
UCLASS()
class EXTRACTGAMECHARACTER_API UExtraGameAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UExtraGameAttributeSet();

	// 属性复制相关
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 属性变化前回调（Clamp 用）
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	// 属性变化后回调（BaseValue → CurrentValue，触发 GameplayEffect 执行）
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// ── 核心属性 ─────────────────────────────────────────

	// 当前生命值
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, Health);

	// 最大生命值
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, MaxHealth);

	// 攻击力
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, AttackPower);

	// 当前耐力
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, Stamina);

	// 最大耐力
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, MaxStamina);
	
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat", ReplicatedUsing = OnRep_Shield)
	FGameplayAttributeData Shield;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, Shield);

	// 轻击连段「打满」次数（进入最后一段 +1，封顶 3）。打满 3 次解锁重击，重击触发后清零。纯本地战斗资源，驱动 ComboCount UI/材质，不做网络复制。
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Combat")
	FGameplayAttributeData ComboCount;
	ATTRIBUTE_ACCESSORS(UExtraGameAttributeSet, ComboCount);

protected:
	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);
	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);
	UFUNCTION()
	virtual void OnRep_Stamina(const FGameplayAttributeData& OldStamina);
	UFUNCTION()
	virtual void OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina);
	UFUNCTION()
	virtual void OnRep_Shield(const FGameplayAttributeData& OldShield);
	
	
	bool bProcessingShieldAbsorption=false;
};

// DataTable 行结构：每个 Actor 类对应一行初始属性值
USTRUCT(BlueprintType)
struct FExtraCharacterAttributeRow : public FTableRowBase
{
	GENERATED_BODY()

	// 使用此行的 Actor 类（当前角色是其子类/实例时匹配）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	TSubclassOf<AActor> CharacterClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float Health = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float AttackPower = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float Stamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float MaxStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attribute")
	float Shield = 0.f;
};
