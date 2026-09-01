#include "GA_HeavyAttack.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"


UGA_HeavyAttack::UGA_HeavyAttack()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag());
	SetAssetTags(AssetTags);
	BlockAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetHeavyAttackAbilityTag());
	// 重击激活时取消正在连段的轻击 GA（避免两套 Montage 叠加）
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetBasicAttackAbilityTag());

	// 霸体期间（SkillGA 表现段）不可激活；后摇段放开后，激活时取消 SkillGA 打断其后摇。
	ActivationBlockedTags.AddTag(UUExtraAbilitySystemStatic::GetUninterruptibleTag());
	CancelAbilitiesWithTag.AddTag(UUExtraAbilitySystemStatic::GetSkill01Tag());

	// 启用通用武器碰撞伤害（基类机制）：服务端监听命中事件并应用 DefaultDamageEffect
	bEnableWeaponDamage = true;

	// 启用锁定目标转向（MR）：攻击朝向锁定目标释放
	bRotateToLockTarget = true;

	FAbilityTriggerData HeavyAttackTrigger;
	HeavyAttackTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	HeavyAttackTrigger.TriggerTag = UUExtraAbilitySystemStatic::GetHeavyAttackInputTag();
	AbilityTriggers.Add(HeavyAttackTrigger);
}

void UGA_HeavyAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 重击成功激活，消耗已打满的 3 层计数（清零后需重新打满 3 次连段才能再重击）
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetNumericAttributeBase(UExtraGameAttributeSet::GetComboCountAttribute(), 0.f);
	}

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,HeavyAttackMontage);
		MontageTask->OnBlendOut.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnInterrupted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCompleted.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->OnCancelled.AddDynamic(this,&ThisClass::K2_EndAbility);
		MontageTask->ReadyForActivation();
	}
}

void UGA_HeavyAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
