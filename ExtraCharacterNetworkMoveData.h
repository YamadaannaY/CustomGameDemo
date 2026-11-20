#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

/**
 * 自定义客户端移动数据：
 * 在原生字段之外，额外携带「客户端本次移动结束时的角色朝向」。
 *
 * 数据来源是 FSavedMove_Character::SavedRotation，它在 PostUpdate 中写入，
 * 即本地预测执行完这一帧移动后的胶囊朝向。
 */
struct FExtraCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
	FRotator ClientRotation = FRotator::ZeroRotator;
	bool bHasClientRotation = false;

	virtual void ClientFillNetworkMoveData(
		const FSavedMove_Character& ClientMove,
		ENetworkMoveType MoveType) override
	{
		FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);

		ClientRotation = ClientMove.SavedRotation;
		bHasClientRotation = true;
	}

	virtual bool Serialize(
		UCharacterMovementComponent& CharacterMovement,
		FArchive& Ar,
		UPackageMap* PackageMap,
		ENetworkMoveType MoveType) override
	{
		if (!FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
		{
			return false;
		}

		// SerializeBits 只写 1 bit；用临时 uint8 避免 bool 其它位残留
		uint8 bHasRotation = bHasClientRotation ? 1 : 0;
		Ar.SerializeBits(&bHasRotation, 1);
		bHasClientRotation = (bHasRotation != 0);

		if (bHasClientRotation)
		{
			bool bLocalSuccess = true;
			ClientRotation.NetSerialize(Ar, PackageMap, bLocalSuccess);
			return bLocalSuccess;
		}

		return true;
	}
};

/**
 * 让 New / Pending / Old 三个槽位都使用自定义 MoveData 类型。
 * 该容器必须是 UCharacterMovementComponent 的持久成员：
 * SetNetworkMoveDataContainer 只保存指针。
 */
struct FExtraCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FExtraCharacterNetworkMoveDataContainer()
	{
		NewMoveData = &CustomMoveData[0];
		PendingMoveData = &CustomMoveData[1];
		OldMoveData = &CustomMoveData[2];
	}

	FExtraCharacterNetworkMoveData CustomMoveData[3];
};
