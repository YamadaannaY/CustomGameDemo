// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "ExtraCharacter.generated.h"

class UAbilitySystemComponent;
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

	// ── 组件访问器 ─────────────────────────────────────────────
	UFUNCTION(BlueprintPure, Category = "Weapon")
	UExtraGameWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

protected:
	virtual void BeginPlay() override;
	
	// ── GAS ────────────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	// ── 武器系统 ───────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UExtraGameWeaponComponent> WeaponComponent;

	// ── 属性集（运行时由 ASC 管理，BeginPlay 中初始化）─────────
	UPROPERTY()
	TObjectPtr<UExtraGameAttributeSet> AttributeSet;

private:
	// 初始化 ASC 的 OwnerActor / AvatarActor
	void InitAbilitySystem();
};
