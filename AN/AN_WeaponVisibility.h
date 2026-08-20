#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_WeaponVisibility.generated.h"

UENUM()
enum class EWeaponVisibilityTarget : uint8
{
	// 操作指定 WeaponTag 对应的单个武器 Mesh
	SingleWeapon,

	// 操作当前WeaponGroup中的所有武器 Mesh
	CurrentGroup,
};

/**
 * - 动画过程中显示 / 隐藏武器 Mesh
 * - SingleWeapon 模式：通过 WeaponTag 指定单个武器
 * - CurrentGroup 模式：统一操作当前武器组中所有武器
 */
UCLASS(meta = (DisplayName = "Weapon Visibility"))
class EXTRACTGAMECHARACTER_API UAN_WeaponVisibility : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	// 操作目标范围
	UPROPERTY(EditAnywhere, Category = "Weapon")
	EWeaponVisibilityTarget Target = EWeaponVisibilityTarget::SingleWeapon;

	// 目标武器 Tag（仅 SingleWeapon 模式使用，对应 WeaponDataAsset 中 的指定Weapon的Tag）
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (EditCondition = "Target == EWeaponVisibilityTarget::SingleWeapon"))
	FGameplayTag WeaponTag;

	// true = 显示，false = 隐藏
	UPROPERTY(EditAnywhere, Category = "Weapon")
	bool bShow = false;
};
