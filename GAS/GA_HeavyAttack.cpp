#include "GA_HeavyAttack.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "ExtractGameCharacter/UExtraAbilitySystemStatic.h"
#include "ExtractGameCharacter/ExtraCharacter.h"
#include "ExtractGameCharacter/ExtraPlayerCharacter.h"
#include "ExtractGameCharacter/Projectile/ExtraArrow.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameAttributeSet.h"
#include "ExtractGameCharacter/WeaponSystem/ExtraGameWeaponComponent.h"

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

	// 弓射为三段箭矢：不启用近战武器轨迹伤害（箭矢由 AExtraArrow 自行结算 GE）
	bEnableWeaponDamage = false;

	// 启用锁定目标转向（MR）：拔弓朝向锁定目标
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

	// 本 GA 按 PerActor 实例复用，跨激活重置时停/快照状态
	FrozenEnemies.Reset();
	bHasAimPoint = false;
	AimTargetLocation = FVector::ZeroVector;

	if (HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		// 时停全场敌方 + 缓存锁定目标位置为瞄准快照
		ApplyTimeFreeze();
		UpdateAimSnapshot();

		// 挂载放箭帧监听：Montage 内 3 个 AN 依次触发 ability.heavyattack.shoot，每收到一次射一箭
		SetupShootListener();

		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, HeavyAttackMontage);
		MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::K2_EndAbility);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::K2_EndAbility);
		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::K2_EndAbility);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::K2_EndAbility);
		MontageTask->ReadyForActivation();
	}
}

void UGA_HeavyAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 无论以何种方式结束（播完/被打断/被取消），都恢复时停冻结
	ReleaseTimeFreeze();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_HeavyAttack::SetupShootListener()
{
	// OnlyTriggerOnce=false：一段 Montage 内三次放箭都要响应
	UAbilityTask_WaitGameplayEvent* WaitShootTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, UUExtraAbilitySystemStatic::GetHeavyAttackShootTag(), nullptr, false, true);
	WaitShootTask->EventReceived.AddDynamic(this, &ThisClass::HandleShootRequest);
	WaitShootTask->ReadyForActivation();
}

void UGA_HeavyAttack::HandleShootRequest(FGameplayEventData EventData)
{
	UE_LOG(LogTemp,Warning,TEXT("Shoot Request Happen"));
	SpawnArrowAtSocket();
}

void UGA_HeavyAttack::ApplyTimeFreeze()
{
	AExtraPlayerCharacter* Char = GetOwningAvatarCharacter();
	UWorld* World = GetWorld();
	if (!Char || !World)
	{
		return;
	}

	for (TActorIterator<AExtraCharacter> It(World); It; ++It)
	{
		AExtraCharacter* Enemy = *It;
		if (!Enemy || Enemy == Char)
		{
			continue;
		}
		if (Enemy->GetGenericTeamId() == Char->GetGenericTeamId())
		{
			continue;
		}

		const UAbilitySystemComponent* EnemyASC = Enemy->GetAbilitySystemComponent();
		if (!EnemyASC || EnemyASC->GetNumericAttribute(UExtraGameAttributeSet::GetHealthAttribute()) <= 0.f)
		{
			continue;
		}

		// 幂等：同一激活内不重复记录同一敌人
		bool bAlreadyFrozen = false;
		for (const FHeavyTimeFreezeEntry& Entry : FrozenEnemies)
		{
			if (Entry.Enemy.Get() == Enemy)
			{
				bAlreadyFrozen = true;
				break;
			}
		}
		if (bAlreadyFrozen)
		{
			continue;
		}

		FHeavyTimeFreezeEntry Entry;
		Entry.Enemy = Enemy;
		Entry.OriginalTimeDilation = Enemy->CustomTimeDilation;
		Enemy->CustomTimeDilation = 0.f;   // 冻结：移动 + 动画 + 组件 Tick 全部停住
		FrozenEnemies.Add(Entry);
	}
}

void UGA_HeavyAttack::ReleaseTimeFreeze()
{
	for (FHeavyTimeFreezeEntry& Entry : FrozenEnemies)
	{
		if (AActor* Enemy = Entry.Enemy.Get())
		{
			Enemy->CustomTimeDilation = Entry.OriginalTimeDilation;
		}
	}
	FrozenEnemies.Reset();
}

void UGA_HeavyAttack::UpdateAimSnapshot()
{
	AExtraPlayerCharacter* Char = GetOwningAvatarCharacter();
	if (!Char)
	{
		return;
	}

	if (AActor* LockTarget = Char->GetLockTarget())
	{
		bHasAimPoint = true;
		AimTargetLocation = LockTarget->GetActorLocation();
	}
}

void UGA_HeavyAttack::SpawnArrowAtSocket()
{
	// 服务端权威生成（单机验证阶段权威端即本地；纯客户端联机的本地表现后续补）
	if (!K2_HasAuthority())
	{
		return;
	}

	AExtraPlayerCharacter* Char = GetOwningAvatarCharacter();
	if (!Char || !ArrowActorClass)
	{
		return;
	}

	UExtraGameWeaponComponent* WeaponComp = Char->GetWeaponComponent();
	if (!WeaponComp)
	{
		return;
	}

	UStaticMeshComponent* BowMesh = WeaponComp->GetWeaponMeshByTag(BowWeaponTag);
	if (!BowMesh || !BowMesh->DoesSocketExist(ArrowSpawnSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_HeavyAttack] 未找到弓或出箭Socket %s"), *ArrowSpawnSocketName.ToString());
		return;
	}

	const FVector SpawnLoc = BowMesh->GetSocketLocation(ArrowSpawnSocketName);

	// 方向：有瞄准快照 → 朝快照点；无快照 → 角色正前
	FVector FireDir = Char->GetActorForwardVector();
	if (bHasAimPoint)
	{
		const FVector ToTarget = AimTargetLocation - SpawnLoc;
		if (ToTarget.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			FireDir = ToTarget.GetSafeNormal();
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Char;
	SpawnParams.Instigator = Char;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AExtraArrow* Arrow = World->SpawnActor<AExtraArrow>(ArrowActorClass, SpawnLoc, FireDir.Rotation(), SpawnParams);
	if (!Arrow)
	{
		return;
	}

	// 若 GA 侧配置了箭身 mesh，覆盖到生成的箭上（否则用 ArrowActorClass 默认/蓝图 mesh）
	if (!ArrowStaticMesh.IsNull())
	{
		if (UStaticMesh* Mesh = ArrowStaticMesh.LoadSynchronous())
		{
			Arrow->SetArrowMesh(Mesh);
		}
	}

	Arrow->InitShot(Char, ArrowDamageEffect, static_cast<int32>(GetAbilityLevel()), FireDir, ArrowSpeed, ArrowLifeTime);
}
