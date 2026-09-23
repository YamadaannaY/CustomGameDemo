#include "ANS_CombatCamera.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

bool UANS_CombatCamera::CheckRequiredTag(USkeletalMeshComponent* MeshComp) const
{
	if (!RequiredOwnerTag.IsValid())
	{
		return true;
	}

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner());
	return ASC && ASC->HasMatchingGameplayTag(RequiredOwnerTag);
}

void UANS_CombatCamera::PushCameraRequest(USkeletalMeshComponent* MeshComp)
{
	if (MeshComp && MeshComp->GetOwner())
	{
		if (UCombatCameraComponent* CamComp = MeshComp->GetOwner()->FindComponentByClass<UCombatCameraComponent>())
		{
			CachedRequestIds.Add(MeshComp, CamComp->PushRequest(CameraRequest));
		}
	}
}

void UANS_CombatCamera::PopRequestForMesh(USkeletalMeshComponent* MeshComp)
{
	int32 RequestId = INDEX_NONE;
	if (!CachedRequestIds.RemoveAndCopyValue(MeshComp, RequestId))
	{
		return;
	}

	if (MeshComp && MeshComp->GetOwner())
	{
		if (UCombatCameraComponent* CamComp = MeshComp->GetOwner()->FindComponentByClass<UCombatCameraComponent>())
		{
			CamComp->PopRequest(RequestId);
		}
	}
}

void UANS_CombatCamera::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	// 顺手清掉已销毁 mesh 留下的条目。TWeakObjectPtr 失效后哈希会变成 0，
	// 后续 Remove 用原 key 再也命中不了，只能靠这里扫掉，否则长期运行会累积。
	for (auto It = CachedRequestIds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = PendingMeshComps.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}

	// 上一次区间没走到 NotifyEnd（蒙太奇被打断后重播）会留下旧请求，先撤销，避免它滞留在栈里
	PopRequestForMesh(MeshComp);
	PendingMeshComps.Remove(MeshComp);

	if (!CheckRequiredTag(MeshComp))
	{
		//开头帧增加一次复核操作，解决可能的时序问题
		PendingMeshComps.Add(MeshComp);
		return;
	}

	PushCameraRequest(MeshComp);
}

void UANS_CombatCamera::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// 已提交，或等待复核
	if (CachedRequestIds.Contains(MeshComp) || !PendingMeshComps.Contains(MeshComp))
	{
		return;
	}

	if (!CheckRequiredTag(MeshComp))
	{
		return;
	}

	// 条件在区间内满足了：此时移除缓存并补提交
	PendingMeshComps.Remove(MeshComp);
	PushCameraRequest(MeshComp);
}

void UANS_CombatCamera::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	PendingMeshComps.Remove(MeshComp);
	PopRequestForMesh(MeshComp);
}

FString UANS_CombatCamera::GetNotifyName_Implementation() const
{
	return TEXT("CombatCamera");
}
