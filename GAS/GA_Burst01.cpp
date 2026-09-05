// Fill out your copyright notice in the Description page of Project Settings.

#include "GA_Burst01.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"

UGA_Burst01::UGA_Burst01()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetBurst01Tag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAbilityTag());

	FAbilityTriggerData BurstTrigger;
	BurstTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	BurstTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetUltimateInputTag();
	AbilityTriggers.Add(BurstTrigger);

	// 仅第一形态（State.Phase1 在 ASC owned tags 上）可激活：
	// 本 GA 常驻授予（Innate），靠该 tag 门控，切到第二形态后 tag 移除 → 大招自然失效，
	// 从而避免"运行时把自己所在的武器组卸掉导致自杀"的矛盾。
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetPhase1StateTag());

	bEnableUninterruptible = true;

	// Burst 不用武器轨迹碰撞：默认关闭武器伤害，改走「角色中心范围伤害」。
	// Montage 想要的伤害帧各放一个 AN_SendGameplayEvent(ability.area.damage)，
	// 每收到一次以角色为中心做半径检测，对范围内所有敌方单位统一结算伤害 GE。
	bEnableWeaponDamage = false;
	bEnableAreaDamage = true;
	AreaDamageRadius = 1500.f;
}

void UGA_Burst01::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UE_LOG(LogTemp,Warning,TEXT("[GA_Burst01] : The GA has been activated"));

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 激活即切第二形态：整段开大 Montage 都用第二阶段武器组演出。
	// 本 GA spec 常驻 Innate、不在武器组的 ActiveAbilityHandles 里，
	// SwitchWeaponGroup 卸载 Phase1 组时清的是攻击技能组，不会 Clear 掉本 GA 自身。
	SwitchToBurstForm();

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,BurstMontage);
	MontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
	MontageTask->ReadyForActivation();
	
	UAbilityTask_WaitGameplayEvent* WaitChangeStateTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,FGameplayTag::RequestGameplayTag("ability.Burst.changestate"));
	WaitChangeStateTask->EventReceived.AddDynamic(this,&ThisClass::ChangeToSecondState);
	WaitChangeStateTask->ReadyForActivation();
}

bool UGA_Burst01::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (SlotMaterialMap.Num()==0) return false ; 
	if (!BurstMontage) return false ; 
	
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UGA_Burst01::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                             const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Burst01::ChangeToSecondState(FGameplayEventData PayLoad)
{
	USkeletalMeshComponent* MeshComp = Cast<AExtraCharacter>(GetAvatarActorFromActorInfo())->GetMesh();
	if (MeshComp)
	{
		for (const auto& pair : SlotMaterialMap)
		{
			MeshComp->SetMaterialByName(pair.Key,pair.Value);
		}
	}
}

void UGA_Burst01::SwitchToBurstForm()
{
	if (!BurstTargetWeaponGroupTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Burst01] SwitchToBurstForm: BurstTargetWeaponGroupTag is not set."));
		return;
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	UExtraGameWeaponComponent* WeaponComp = Avatar->FindComponentByClass<UExtraGameWeaponComponent>();
	if (!WeaponComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Burst01] SwitchToBurstForm: No UExtraGameWeaponComponent on avatar."));
		return;
	}

	if (!WeaponComp->SwitchWeaponGroup(BurstTargetWeaponGroupTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Burst01] SwitchToBurstForm: SwitchWeaponGroup to '%s' failed."),
			*BurstTargetWeaponGroupTag.ToString());
	}
}
