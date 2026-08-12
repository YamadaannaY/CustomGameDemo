#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "ExtraCharacter.generated.h"

class UExtraAbilitySystemComponent;
class UExtraGameWeaponComponent;
class UExtraGameAttributeSet;

UCLASS()
class EXTRACTGAMECHARACTER_API AExtraCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AExtraCharacter(const FObjectInitializer& ObjectInitializer);

	// ── IAbilitySystemInterface ────────────────────────────────
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// ── GAS 初始化（由 Controller OnPossess 调用）─────────────
	// 先调用 InitAbilityActorInfo，再调用 ASC 的 ServerSideInit
	virtual void ServerSideInit();

	// ── 组件访问器 ─────────────────────────────────────────────
	UFUNCTION(BlueprintPure, Category = "Weapon")
	UExtraGameWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	// GAS 是否已初始化（防止 OnPossess 重复调用）
	bool bGASInitialized = false;

protected:
	virtual void BeginPlay() override;
	
	// ── GAS ────────────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UExtraAbilitySystemComponent> AbilitySystemComponent;

	// ── 武器系统 ───────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UExtraGameWeaponComponent> WeaponComponent;
};
