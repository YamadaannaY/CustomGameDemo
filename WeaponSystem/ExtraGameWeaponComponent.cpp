// Copyright Yu. All Rights Reserved.

#include "ExtraGameWeaponComponent.h"
#include "ExtraGameWeaponData.h"
#include "ExtractGameCharacter/GAS/ExtraGameplayAbility.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"


UExtraGameWeaponComponent::UExtraGameWeaponComponent()
{
	// Fade 淡入淡出由 Tick 驱动（无 Fade 请求时自动关闭 Tick）
	PrimaryComponentTick.bCanEverTick = true;

	// 默认命中事件走通用伤害事件（GA 基类在服务端监听该 Tag，攻击 GA 统一继承）
	TraceEventTag = UUExtraAbilitySystemStatic::GetAbilityDamageEventTag();
}

void UExtraGameWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	
	OnWeaponGroupChanged.AddDynamic(this,&ThisClass::WeaponGroupChangeDebug);
}

void UExtraGameWeaponComponent::WeaponGroupChangeDebug(FGameplayTag OldGroupTag, FGameplayTag NewGroupTag)
{
	GEngine->AddOnScreenDebugMessage(-1, 7.0f, FColor::Green, 
		FString::Printf(TEXT("OldGroup Tag : %s , NewGroup Tag : %s"),*OldGroupTag.ToString(),*NewGroupTag.ToString()));
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
	
	//装备默认装备组武器（没有配置默认WeaponTag会安全空返回）
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

	// 清理 Fade 运行时状态与缓存的 DMI
	WeaponFadeStates.Empty();
	WeaponDynamicMIs.Empty();

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

const FExtraGameWeaponEntry* UExtraGameWeaponComponent::ResolveWeaponEntry(FGameplayTag WeaponTag) const
{
	if (!WeaponDataAsset || !WeaponTag.IsValid())
	{
		return nullptr;
	}

	// 优先当前组，其次全局表（跨组保留的 Mesh 也能解析到数据）
	if (const FExtraGameWeaponGroup* Cur = GetWeaponGroupByTag(CurrentGroupTag))
	{
		if (const FExtraGameWeaponEntry* Entry = Cur->FindWeaponEntry(WeaponTag))
		{
			return Entry;
		}
	}

	if (const FExtraGameWeaponGroup* OwnerGroup = WeaponDataAsset->FindGroupContainingWeapon(WeaponTag))
	{
		return OwnerGroup->FindWeaponEntry(WeaponTag);
	}

	return nullptr;
}

bool UExtraGameWeaponComponent::SetWeaponAttachSocket(FGameplayTag WeaponTag, FName SocketName)
{
	const FExtraGameWeaponEntry* Entry = ResolveWeaponEntry(WeaponTag);
	if (!Entry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] SetWeaponAttachSocket: no weapon data for '%s'."), *WeaponTag.ToString());
		return false;
	}

	UStaticMeshComponent* MeshComp = GetWeaponMeshByTag(WeaponTag);
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] SetWeaponAttachSocket: no mesh for '%s'."), *WeaponTag.ToString());
		return false;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* SkeletalMesh = OwnerChar ? OwnerChar->GetMesh() : nullptr;
	if (!SkeletalMesh)
	{
		return false;
	}

	// 空 / None / 默认挂点 → 复位到默认 AttachSocketName；否则必须是该武器的合法显示挂点
	FName TargetSocket = SocketName != NAME_None ? SocketName : Entry->AttachSocketName;
	if (!Entry->IsDisplaySocket(TargetSocket))
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] SetWeaponAttachSocket: socket '%s' is not in weapon '%s' display sockets."),
			*SocketName.ToString(), *WeaponTag.ToString());
		return false;
	}

	if (!SkeletalMesh->DoesSocketExist(TargetSocket))
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] SetWeaponAttachSocket: skeleton socket '%s' not found."), *TargetSocket.ToString());
		return false;
	}

	// 已在目标挂点则无需重挂
	if (MeshComp->GetAttachSocketName() == TargetSocket)
	{
		return true;
	}

	MeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	MeshComp->AttachToComponent(SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform, TargetSocket);
	MeshComp->SetRelativeTransform(Entry->RelativeTransform);

	return true;
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

	// 如果已经是同一武器组再次装备 → 只需确保 Mesh 可见性正确
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

			// 装备 / 切换为瞬时到位，无淡入动画
			InitWeaponFade(Entry.WeaponTag, bEntryVisible);
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
				RequestWeaponFade(Entry.WeaponTag, true);
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
			RequestWeaponFade(Entry.WeaponTag, true);
		}
	}
}

void UExtraGameWeaponComponent::HideWeapon()
{
	bWeaponVisible = false;

	if (const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup())
	{
		for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
		{
			RequestWeaponFade(Entry.WeaponTag, false);
		}
	}
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

	RequestWeaponFade(WeaponTag, true);
}

bool UExtraGameWeaponComponent::ShowWeaponEntryOnSocket(FGameplayTag WeaponTag, FName SocketName)
{
	if (!SetWeaponAttachSocket(WeaponTag, SocketName))
	{
		return false;
	}

	ShowWeaponEntry(WeaponTag);
	return true;
}

void UExtraGameWeaponComponent::HideWeaponEntry(FGameplayTag WeaponTag)
{
	if (!WeaponTag.IsValid())
	{
		return;
	}

	HiddenWeaponEntries.Add(WeaponTag);

	RequestWeaponFade(WeaponTag, false);
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

	// 为每个 Material Slot 创建动态材质实例并缓存（用于 FadeAmount 控制），默认不透明
	TArray<TObjectPtr<UMaterialInstanceDynamic>>& DMIs = WeaponDynamicMIs.FindOrAdd(Entry.WeaponTag);
	for (int32 Slot = 0; Slot < MeshComp->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* DMI = MeshComp->CreateAndSetMaterialInstanceDynamic(Slot))
		{
			DMI->SetScalarParameterValue(FadeParameterName, -1.f);
			DMIs.Add(DMI);
		}
	}

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

	WeaponFadeStates.Remove(WeaponTag);
	WeaponDynamicMIs.Remove(WeaponTag);
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
		// 瞬时隐藏不参与 Fade 动画，清掉运行时状态
		WeaponFadeStates.Remove(Pair.Key);
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
		// 瞬时隐藏（卸载 / 切换组），清掉运行时 Fade 状态
		WeaponFadeStates.Remove(Entry.WeaponTag);
	}
}

// ──────────────────────────────────────────────────────────────
// 显隐 Fade（私有，材质 FadeAmount 驱动）
// ──────────────────────────────────────────────────────────────

void UExtraGameWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (WeaponFadeStates.Num() == 0)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// FadeAmount 跨度为 2（-1 → 1），恒速过渡，总时长 ≈ WeaponFadeDuration
	const float FadeSpeed = 2.f / FMath::Max(WeaponFadeDuration, KINDA_SMALL_NUMBER);

	bool bAnyFading = false;
	for (auto& Pair : WeaponFadeStates)
	{
		FExtraWeaponFadeState& State = Pair.Value;

		if (FMath::Abs(State.TargetFade - State.CurrentFade) <= 0.005f)
		{
			// 已到位：收敛到精确值；完全透明时关闭渲染省性能
			State.CurrentFade = State.TargetFade;
			if (State.TargetFade >= 1.f - KINDA_SMALL_NUMBER)
			{
				if (UStaticMeshComponent* Mesh = GetWeaponMeshByTag(Pair.Key))
				{
					Mesh->SetVisibility(false);
				}
			}
			continue;
		}

		bAnyFading = true;
		State.CurrentFade = FMath::FInterpConstantTo(State.CurrentFade, State.TargetFade, DeltaTime, FadeSpeed);
		SetWeaponFadeValue(Pair.Key, State.CurrentFade);
	}

	if (!bAnyFading)
	{
		SetComponentTickEnabled(false);
	}
}

void UExtraGameWeaponComponent::RequestWeaponFade(FGameplayTag WeaponTag, bool bFadeIn)
{
	UStaticMeshComponent* Mesh = GetWeaponMeshByTag(WeaponTag);
	if (!Mesh)
	{
		return;
	}

	const float Target = bFadeIn ? -1.f : 1.f;

	// 淡入前先恢复可见，避免淡出完成后渲染被关闭导致无显示
	if (bFadeIn)
	{
		Mesh->SetVisibility(true);
	}

	FExtraWeaponFadeState& State = WeaponFadeStates.FindOrAdd(WeaponTag);
	if (FMath::IsNearlyEqual(State.CurrentFade, Target, 0.005f))
	{
		// 已到位：直接收敛，不启动动画
		State.CurrentFade = Target;
		State.TargetFade = Target;
		return;
	}

	State.TargetFade = Target;
	if (!IsComponentTickEnabled())
	{
		SetComponentTickEnabled(true);
	}
}

void UExtraGameWeaponComponent::InitWeaponFade(FGameplayTag WeaponTag, bool bVisible)
{
	const float Value = bVisible ? -1.f : 1.f;

	SetWeaponFadeValue(WeaponTag, Value);

	FExtraWeaponFadeState& State = WeaponFadeStates.FindOrAdd(WeaponTag);
	State.CurrentFade = Value;
	State.TargetFade = Value;
}

void UExtraGameWeaponComponent::SetWeaponFadeValue(FGameplayTag WeaponTag, float Value)
{
	if (const TArray<TObjectPtr<UMaterialInstanceDynamic>>* DMIs = WeaponDynamicMIs.Find(WeaponTag))
	{
		for (const TObjectPtr<UMaterialInstanceDynamic>& DMI : *DMIs)
		{
			if (DMI)
			{
				DMI->SetScalarParameterValue(FadeParameterName, Value);
			}
		}
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

	//EffectContext
	FGameplayEffectContextHandle ContextHandle = OwnerASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);
	ContextHandle.AddInstigator(GetOwner(), GetOwner());

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

// ──────────────────────────────────────────────────────────────
// 轨迹伤害扫描
// ──────────────────────────────────────────────────────────────

void UExtraGameWeaponComponent::BeginWeaponTrace()
{
	// 直接重置并开启：即使上一次窗口因蒙太奇异常终止而未正常关闭，也不会卡死后续扫描
	if (!GatherTraceSocketsFromCurrentGroup())
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] BeginWeaponTrace: current weapon group has no valid trace sockets."));
		return;
	}

	bTraceActive = true;
	TraceSocketPrevLocations.Empty();
	AlreadyHitActors.Empty();

	// 立即记录各 Socket 起始位置，使首个 NotifyTick 就能开始扫描（不浪费窗口第一帧）
	for (const FName& SocketName : ActiveTraceSockets)
	{
		if (UStaticMeshComponent* MeshComp = ActiveTraceSocketToMesh.FindRef(SocketName))
		{
			TraceSocketPrevLocations.Add(SocketName, MeshComp->GetSocketLocation(SocketName));
		}
	}
}

void UExtraGameWeaponComponent::TickWeaponTrace()
{
	if (!bTraceActive)
	{
		return;
	}

	for (const FName& SocketName : ActiveTraceSockets)
	{
		UStaticMeshComponent* MeshComp = ActiveTraceSocketToMesh.FindRef(SocketName);
		if (!MeshComp)
		{
			continue;
		}

		const FVector Curr = MeshComp->GetSocketLocation(SocketName);

		FVector* PrevPtr = TraceSocketPrevLocations.Find(SocketName);
		
		// 对首帧进行特别处理：仅记录起点，不扫描（避免从原点拉出整条射线）
		if (!PrevPtr)
		{
			TraceSocketPrevLocations.Add(SocketName, Curr);
			continue;
		}
		
		//进行插值扫描
		TraceSocketSegment(SocketName, *PrevPtr, Curr);
		
		//更新Pre指针
		*PrevPtr = Curr;
	}
}

void UExtraGameWeaponComponent::EndWeaponTrace()
{
	if (!bTraceActive)
	{
		return;
	}

	bTraceActive = false;

	// 即时结算模式下，NotifyEnd 只负责清空黑名单，等待下一次攻击窗口重新累计
	AlreadyHitActors.Empty();
	ActiveTraceSockets.Empty();
	ActiveTraceSocketToMesh.Empty();
	TraceSocketPrevLocations.Empty();
}

bool UExtraGameWeaponComponent::GatherTraceSocketsFromCurrentGroup()
{
	ActiveTraceSockets.Empty();
	ActiveTraceSocketToMesh.Empty();

	const FExtraGameWeaponGroup* Group = GetCurrentWeaponGroup();
	if (!Group)
	{
		return false;
	}

	bool bFoundConfig = false;

	for (const FExtraGameWeaponEntry& Entry : Group->WeaponEntries)
	{
		if (Group->WeaponShouldNotCauseDamage.Contains(Entry.WeaponTag))
		{
			continue;
		}
		
		if (HiddenWeaponEntries.Contains(Entry.WeaponTag))
		{
			continue;
		}
		
		if (Entry.TraceSockets.Num() == 0)
		{
			continue;
		}

		UStaticMeshComponent* MeshComp = GetWeaponMeshByTag(Entry.WeaponTag);
		if (!MeshComp)
		{
			continue;
		}

		// 组内第一把有 TraceSocket 的武器决定本窗口扫描配置
		if (!bFoundConfig)
		{
			ActiveTraceConfig = Entry.TraceConfig;
			bFoundConfig = true;
		}

		for (const FName& SocketName : Entry.TraceSockets)
		{
			if (SocketName == NAME_None)
			{
				continue;
			}

			if (!MeshComp->DoesSocketExist(SocketName))
			{
				UE_LOG(LogTemp, Warning, TEXT("[WeaponComponent] TraceSocket '%s' not found on weapon '%s'."),
					*SocketName.ToString(), *Entry.WeaponTag.ToString());
				continue;
			}

			ActiveTraceSockets.Add(SocketName);
			ActiveTraceSocketToMesh.Add(SocketName, MeshComp);
		}
	}
	return ActiveTraceSockets.Num() > 0;
}

void UExtraGameWeaponComponent::TraceSocketSegment(const FName SocketName, const FVector& Prev, const FVector& Curr)
{
	const FVector Delta = Curr - Prev;
	const float Distance = Delta.Size();
	if (Distance < KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 帧间位移过大时细分为多个子步，避免高速挥动直接穿过命中体
	//获取走完这段距离需要的步进数，进而获取每个步进的具体向量值
	const int32 Steps = FMath::Clamp(
		FMath::CeilToInt(Distance / FMath::Max(ActiveTraceConfig.MaxStepDistance, 1.f)),
		1, FMath::Max(ActiveTraceConfig.MaxSubSteps, 1));
	const FVector StepVec = Delta / static_cast<float>(Steps);
	const float TraceRadius = FMath::Max(ActiveTraceConfig.TraceRadius, 0.1f);

	AActor* Owner = GetOwner();

	FCollisionQueryParams QueryParams(TEXT("WeaponTrace"), /*bTraceComplex*/ false);
	if (Owner)
	{
		QueryParams.AddIgnoredActor(Owner);
	}
	// 忽略本组件生成的全部武器 Mesh，防止命中自身武器
	for (const auto& Pair : SpawnedWeaponMeshes)
	{
		if (Pair.Value)
		{
			QueryParams.AddIgnoredComponent(Pair.Value.Get());
		}
	}

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(TraceRadius);
	const ECollisionChannel Channel = WeaponTraceChannel.GetValue();

	//将路径切分为Steps个首尾相连的小线段
	for (int32 i = 0; i < Steps; ++i)
	{
		const FVector SegStart = Prev + StepVec * static_cast<float>(i);
		const FVector SegEnd = Prev + StepVec * static_cast<float>(i + 1);

		TArray<FHitResult> Hits;
		if (!GetWorld())
		{
			return;
		}
		GetWorld()->SweepMultiByChannel(Hits, SegStart, SegEnd, FQuat::Identity, Channel, Sphere, QueryParams);

		if (bDrawWeaponTraceDebug)
		{
			DrawDebugLine(GetWorld(), SegStart, SegEnd, FColor::Red, false, 0.2f);
			DrawDebugSphere(GetWorld(), SegEnd, TraceRadius, 8, FColor::Green, false, 0.2f);
		}

		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || AlreadyHitActors.Contains(TWeakObjectPtr<AActor>(HitActor)))
			{
				continue;
			}

			// 命中即结算：当帧加入黑名单并立即发事件，GA 收到后当帧应用 GE；
			// 同一窗口内重复扫到同一目标被黑名单拦下，不重复结算。
			AlreadyHitActors.Add(TWeakObjectPtr<AActor>(HitActor));
			SendHitEventForActor(HitActor);
		}
	}
}

void UExtraGameWeaponComponent::SendHitEventForActor(AActor* HitActor)
{
	AActor* Owner = GetOwner();
	if (!Owner || !HitActor)
	{
		return;
	}

	FGameplayTag EventTag = TraceEventTag;
	if (!EventTag.IsValid())
	{
		EventTag = UUExtraAbilitySystemStatic::GetAbilityDamageEventTag();
	}

	FGameplayAbilityTargetData_ActorArray* ActorArray = new FGameplayAbilityTargetData_ActorArray();
	ActorArray->TargetActorArray.Add(HitActor);

	FGameplayAbilityTargetDataHandle TargetData;
	TargetData.Add(ActorArray); 

	FGameplayEventData EventData;
	EventData.Instigator = Owner;
	EventData.TargetData = TargetData;
	
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, EventData);
}