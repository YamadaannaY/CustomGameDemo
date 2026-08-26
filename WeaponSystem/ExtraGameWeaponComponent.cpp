// Copyright Yu. All Rights Reserved.

#include "ExtraGameWeaponComponent.h"
#include "ExtraGameWeaponData.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"


UExtraGameWeaponComponent::UExtraGameWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UExtraGameWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UExtraGameWeaponComponent::OnASCInitialized()
{
	CacheOwnerASC();

	if (!WeaponDataAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("[WeaponComponent] OnASCInitialized: WeaponDataAsset is null! Configure it on the Character Blueprint."));
		return;
	}

	if (!WeaponDataAsset->DefaultWeaponGroupTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[WeaponComponent] OnASCInitialized: DefaultWeaponGroupTag is invalid in WeaponDataAsset '%s'."), *WeaponDataAsset->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[WeaponComponent] OnASCInitialized: Equipping default weapon group '%s'."), *WeaponDataAsset->DefaultWeaponGroupTag.ToString());
	EquipWeaponGroup(WeaponDataAsset->DefaultWeaponGroupTag);
}

void UExtraGameWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清理 GAS 状态
	RemoveGrantedAbilities();
	RemoveGrantedEffects();

	// 清理所有生成的 Mesh
	for (auto& Pair : SpawnedWeaponMeshes)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	SpawnedWeaponMeshes.Empty();

	Super::EndPlay(EndPlayReason);
}

void UExtraGameWeaponComponent::CacheOwnerASC()
{
	if (!OwnerASC)
	{
		if (AActor* Owner = GetOwner())
		{
			OwnerASC = Owner->FindComponentByClass<UAbilitySystemComponent>();
		}
	}
}

// ──────────────────────────────────────────────────────────────
// EquipWeaponGroup
// ──────────────────────────────────────────────────────────────

bool UExtraGameWeaponComponent::EquipWeaponGroup(FGameplayTag GroupTag)
{
	// 校验
	if (!GroupTag.IsValid() || !WeaponDataAsset)
	{
		return false;
	}

	const FExtraGameWeaponGroup* Group = WeaponDataAsset->FindGroup(GroupTag);
	if (!Group)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] EquipWeaponGroup: GroupTag '%s' not found in DataAsset."), *GroupTag.ToString());
		return false;
	}

	CacheOwnerASC();

	// 如果已经是同一武器组 → 只需确保 Mesh 可见性正确
	if (CurrentGroupTag == GroupTag)
	{
		ShowWeapon();
		return true;
	}

	const FGameplayTag OldTag = CurrentGroupTag;
	const FExtraGameWeaponGroup* OldGroup = WeaponDataAsset->FindGroup(OldTag);

	// 1. 先卸载旧武器组
	UnequipWeaponGroup();

	// 2. 生成 / 显示该组所有 Mesh
	for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
	{
		if (!Entry.WeaponTag.IsValid())
		{
			continue;
		}

		UStaticMeshComponent* MeshComp = SpawnedWeaponMeshes.FindRef(Entry.WeaponTag);
		if (!MeshComp)
		{
			MeshComp = SpawnWeaponMesh(Entry);
			if (MeshComp)
			{
				SpawnedWeaponMeshes.Add(Entry.WeaponTag, MeshComp);
			}
		}

		if (MeshComp)
		{
			// 可见性 = 整体可见 && 未被手动隐藏
			const bool bEntryVisible = bWeaponVisible && !HiddenWeaponEntries.Contains(Entry.WeaponTag);
			MeshComp->SetVisibility(bEntryVisible);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// 3. 应用 GE
	ApplyWeaponGroupEffects(*Group);

	// 4. 授予 GA
	GrantWeaponGroupAbilities(*Group);

	// 5. 更新 ASC Tags
	UpdateCharacterTags(OldGroup, Group);

	// 6. 更新状态
	CurrentGroupTag = GroupTag;

	// 7. 广播事件
	OnWeaponGroupChanged.Broadcast(OldTag, GroupTag);

	return true;
}

// ──────────────────────────────────────────────────────────────
// UnequipWeaponGroup
// ──────────────────────────────────────────────────────────────

void UExtraGameWeaponComponent::UnequipWeaponGroup()
{
	if (!CurrentGroupTag.IsValid())
	{
		return;
	}

	// 1. 移除 GA
	RemoveGrantedAbilities();

	// 2. 移除 GE
	RemoveGrantedEffects();

	// 3. 移除原有 AdditionalTags
	if (const FExtraGameWeaponGroup* OldGroup = WeaponDataAsset ? WeaponDataAsset->FindGroup(CurrentGroupTag) : nullptr)
	{
		if (OwnerASC && OldGroup->AdditionalTags.Num() > 0)
		{
			OwnerASC->RemoveLooseGameplayTags(OldGroup->AdditionalTags);
		}
	}

	// 4. 隐藏该组所有 Mesh（保留不销毁，方便下次快速切换）
	HideGroupWeaponMeshes(CurrentGroupTag);

	// 5. 清空逐条目隐藏状态（切换组后重置）
	HiddenWeaponEntries.Empty();

	// 6. 重置 Tag
	CurrentGroupTag = FGameplayTag();
}

// ──────────────────────────────────────────────────────────────
// SwitchWeaponGroup / CycleToNextWeaponGroup
// ──────────────────────────────────────────────────────────────

bool UExtraGameWeaponComponent::SwitchWeaponGroup(FGameplayTag NewGroupTag)
{
	if (!NewGroupTag.IsValid())
	{
		return false;
	}

	// 相同 Tag → 空操作
	if (CurrentGroupTag == NewGroupTag)
	{
		return true;
	}

	// 先全部隐藏
	HideAllWeaponMeshes();

	// 切换
	return EquipWeaponGroup(NewGroupTag);
}

void UExtraGameWeaponComponent::ShowWeapon()
{
	bWeaponVisible = true;

	if (const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup())
	{
		for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
		{
			// 跳过手动隐藏的条目
			if (!HiddenWeaponEntries.Contains(Entry.WeaponTag))
			{
				SetWeaponMeshVisibility(Entry.WeaponTag, true);
			}
		}
	}
}

void UExtraGameWeaponComponent::ShowAllWeapons()
{
	bWeaponVisible = true;
	HiddenWeaponEntries.Empty();

	if (const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup())
	{
		for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
		{
			SetWeaponMeshVisibility(Entry.WeaponTag, true);
		}
	}
}

void UExtraGameWeaponComponent::HideWeapon()
{
	bWeaponVisible = false;
	HideGroupWeaponMeshes(CurrentGroupTag);
}

// ──────────────────────────────────────────────────────────────
// 逐武器显隐
// ──────────────────────────────────────────────────────────────

void UExtraGameWeaponComponent::ShowWeaponEntry(FGameplayTag WeaponTag)
{
	if (!WeaponTag.IsValid())
	{
		return;
	}

	HiddenWeaponEntries.Remove(WeaponTag);

	SetWeaponMeshVisibility(WeaponTag, true);
	
}

void UExtraGameWeaponComponent::HideWeaponEntry(FGameplayTag WeaponTag)
{
	if (!WeaponTag.IsValid())
	{
		return;
	}

	HiddenWeaponEntries.Add(WeaponTag);
	SetWeaponMeshVisibility(WeaponTag, false);
}

bool UExtraGameWeaponComponent::IsWeaponEntryVisible(FGameplayTag WeaponTag) const
{
	if (!WeaponTag.IsValid())
	{
		return false;
	}

	// 可见条件：整体可见 && 未被手动隐藏 && Mesh 组件存在
	return bWeaponVisible && !HiddenWeaponEntries.Contains(WeaponTag) && SpawnedWeaponMeshes.Contains(WeaponTag);
}

// ──────────────────────────────────────────────────────────────
// 查询
// ──────────────────────────────────────────────────────────────

const FExtraGameWeaponGroup* UExtraGameWeaponComponent::GetCurrentWeaponGroup() const
{
	return GetWeaponGroupByTag(CurrentGroupTag);
}

const FExtraGameWeaponGroup* UExtraGameWeaponComponent::GetWeaponGroupByTag(FGameplayTag GroupTag) const
{
	if (WeaponDataAsset)
	{
		return WeaponDataAsset->FindGroup(GroupTag);
	}
	return nullptr;
}

bool UExtraGameWeaponComponent::GetCurrentWeaponGroupBP(FExtraGameWeaponGroup& OutGroup) const
{
	if (const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup())
	{
		OutGroup = *Group;
		return true;
	}
	return false;
}

bool UExtraGameWeaponComponent::GetWeaponGroupByTagBP(FGameplayTag GroupTag, FExtraGameWeaponGroup& OutGroup) const
{
	if (const FExtraGameWeaponGroup* Group = GetWeaponGroupByTag(GroupTag))
	{
		OutGroup = *Group;
		return true;
	}
	return false;
}

UStaticMeshComponent* UExtraGameWeaponComponent::GetWeaponMeshByTag(FGameplayTag WeaponTag) const
{
	if (WeaponTag.IsValid())
	{
		if (const TObjectPtr<UStaticMeshComponent>* Found = SpawnedWeaponMeshes.Find(WeaponTag))
		{
			return Found->Get();
		}
	}
	return nullptr;
}

TArray<UStaticMeshComponent*> UExtraGameWeaponComponent::GetCurrentGroupMeshes() const
{
	TArray<UStaticMeshComponent*> Result;

	if (const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup())
	{
		for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
		{
			if (const TObjectPtr<UStaticMeshComponent>* Found = SpawnedWeaponMeshes.Find(Entry.WeaponTag))
			{
				if (*Found)
				{
					Result.Add(Found->Get());
				}
			}
		}
	}

	return Result;
}

// ──────────────────────────────────────────────────────────────
// Mesh 管理（私有）
// ──────────────────────────────────────────────────────────────

UStaticMeshComponent* UExtraGameWeaponComponent::SpawnWeaponMesh(const FExtraGameWeaponEntry& Entry)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return nullptr;
	}

	// 加载 Mesh 资源 — 当前使用同步加载
	UStaticMesh* LoadedMesh = Entry.WeaponMesh.LoadSynchronous();
	if (!LoadedMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] SpawnWeaponMesh: Failed to load mesh for Tag '%s'."),
			*Entry.WeaponTag.ToString());
		return nullptr;
	}

	// 创建 Mesh 组件
	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(OwnerCharacter,
		MakeUniqueObjectName(OwnerCharacter, UStaticMeshComponent::StaticClass(),
			*FString::Printf(TEXT("WeaponMesh_%s"), *Entry.WeaponTag.GetTagName().ToString())));
	MeshComp->SetStaticMesh(LoadedMesh);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetVisibility(false);  // 先隐藏，EquipWeaponGroup 流程结束后再按需显示
	MeshComp->RegisterComponent();

	// Attach 到角色骨骼 Mesh 的指定 Socket
	USkeletalMeshComponent* OwnerMesh = OwnerCharacter->GetMesh();
	MeshComp->AttachToComponent(OwnerMesh,
		FAttachmentTransformRules::KeepRelativeTransform,
		Entry.AttachSocketName);
	MeshComp->SetRelativeTransform(Entry.RelativeTransform);

	return MeshComp;
}

void UExtraGameWeaponComponent::DestroyWeaponMesh(FGameplayTag WeaponTag)
{
	if (TObjectPtr<UStaticMeshComponent>* Found = SpawnedWeaponMeshes.Find(WeaponTag))
	{
		if (*Found)
		{
			(*Found)->DestroyComponent();
		}
		SpawnedWeaponMeshes.Remove(WeaponTag);
	}
}

void UExtraGameWeaponComponent::SetWeaponMeshVisibility(FGameplayTag WeaponTag, bool bVisible)
{
	if (const TObjectPtr<UStaticMeshComponent>* Found = SpawnedWeaponMeshes.Find(WeaponTag))
	{
		if (*Found)
		{
			(*Found)->SetVisibility(bVisible);
		}
	}
}

void UExtraGameWeaponComponent::HideAllWeaponMeshes()
{
	for (auto& Pair : SpawnedWeaponMeshes)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(false);
		}
	}
}

void UExtraGameWeaponComponent::HideGroupWeaponMeshes(FGameplayTag GroupTag)
{
	if (!WeaponDataAsset || !GroupTag.IsValid())
	{
		return;
	}

	const FExtraGameWeaponGroup* Group = WeaponDataAsset->FindGroup(GroupTag);
	if (!Group)
	{
		return;
	}

	for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
	{
		SetWeaponMeshVisibility(Entry.WeaponTag, false);
	}
}

// ──────────────────────────────────────────────────────────────
// GAS 状态管理（私有）
// ──────────────────────────────────────────────────────────────

void UExtraGameWeaponComponent::RemoveGrantedAbilities()
{
	if (!OwnerASC)
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : ActiveAbilityHandles)
	{
		if (Handle.IsValid())
		{
			OwnerASC->ClearAbility(Handle);
		}
	}
	ActiveAbilityHandles.Empty();
}

void UExtraGameWeaponComponent::RemoveGrantedEffects()
{
	if (!OwnerASC)
	{
		return;
	}

	for (const FActiveGameplayEffectHandle& Handle : ActiveEffectHandles)
	{
		if (Handle.IsValid())
		{
			OwnerASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	ActiveEffectHandles.Empty();
}

// 武器组 GA 全部以 INDEX_NONE 授予，触发方式由 GA 自身的 AbilityTriggers（InputTag）决定。
void UExtraGameWeaponComponent::GrantWeaponGroupAbilities(const FExtraGameWeaponGroup& Group)
{
	if (!OwnerASC)
	{
		return;
	}

	for (const TSubclassOf<UExtraGameplayAbility>& AbilityClass : Group.GrantedAbilities)
	{
		if (!AbilityClass)
		{
			continue;
		}

		FGameplayAbilitySpecHandle Handle = OwnerASC->GiveAbility(
			FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		ActiveAbilityHandles.Add(Handle);
	}
}

void UExtraGameWeaponComponent::ApplyWeaponGroupEffects(const FExtraGameWeaponGroup& Group)
{
	if (!OwnerASC)
	{
		return;
	}

	// 构建 EffectContext
	FGameplayEffectContextHandle ContextHandle = OwnerASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : Group.GrantedEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		FGameplayEffectSpecHandle SpecHandle = OwnerASC->MakeOutgoingSpec(EffectClass, 1, ContextHandle);
		if (SpecHandle.IsValid())
		{
			FActiveGameplayEffectHandle ActiveHandle = OwnerASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			if (ActiveHandle.IsValid())
			{
				ActiveEffectHandles.Add(ActiveHandle);
			}
		}
	}
}

void UExtraGameWeaponComponent::UpdateCharacterTags(const FExtraGameWeaponGroup* OldGroup, const FExtraGameWeaponGroup* NewGroup)
{
	if (!OwnerASC)
	{
		return;
	}

	// 移除旧武器组的 Tags
	if (OldGroup && OldGroup->AdditionalTags.Num() > 0)
	{
		OwnerASC->RemoveLooseGameplayTags(OldGroup->AdditionalTags);
	}

	// 添加新武器组的 Tags
	if (NewGroup && NewGroup->AdditionalTags.Num() > 0)
	{
		OwnerASC->AddLooseGameplayTags(NewGroup->AdditionalTags);
	}
}