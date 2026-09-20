#include "GA_AttackPro_Phase_2.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"

UGA_AttackPro_Phase_2::UGA_AttackPro_Phase_2()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetAttackProAbilityTag());
	SetAssetTags(AssetTags);

	// 与其他同类互斥 + 阻止自身重入
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetAttackProAbilityTag());
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetAttackProAbilityTag());

	// 双重门控：二阶段+三次居合已打满
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetPhase2StateTag());
	ActivationRequiredTags.AddTag(UUExtraAbilitySystemStatic::GetProReadyTag());

	// 长按普攻达到阈值时，角色按「是否就绪」分派到这个专属 Tag
	FAbilityTriggerData AttackProTrigger;
	AttackProTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AttackProTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetAttackProInputTag();
	AbilityTriggers.Add(AttackProTrigger);
}

void UGA_AttackPro_Phase_2::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (!AttackBurstProMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_AttackPro_Phase_2] 未配置 AttackBurstProMontage，取消激活（不消费 State.ProReady）"));
		K2_EndAbility();
		return;
	}

	// 消费就绪标记，该标记只在离开二阶段时才被重新清零，所以要再触发必须回一阶段重新打满三次居合。
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetLooseGameplayTagCount(UUExtraAbilitySystemStatic::GetProReadyTag(), 0);
	}

	UAbilityTask_PlayMontageAndWait* BurstProMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,AttackBurstProMontage);
	BurstProMontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
	BurstProMontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
	BurstProMontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
	BurstProMontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
	BurstProMontageTask->ReadyForActivation();
}

void UGA_AttackPro_Phase_2::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
