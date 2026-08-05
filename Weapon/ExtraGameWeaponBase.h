#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "ExtraGameWeaponBase.generated.h"

UCLASS()
class EXTRACTGAMECHARACTER_API AExtraGameWeaponBase : public AActor
{
	GENERATED_BODY()

public:
	AExtraGameWeaponBase();

	//获得WeaponCollisionBox
	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const {return WeaponCollisionBox;}
	
	//监听OnComponentBeginOverlap进行回调的函数，此函数包含所有需要在碰撞开始时发生的事件
	UFUNCTION()
	virtual void OnCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult);

	//监听OnComponentEndOverlap进行回调的函数，此函数包含所有需要在碰撞结束时发生的事件
	UFUNCTION()
	virtual void OnCollisionBoxEndOverlap(UPrimitiveComponent*OverlappedComponent, AActor*OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
private:
	//武器Mesh
	UPROPERTY(VisibleAnywhere,Category="Weapons")
	UStaticMeshComponent* WeaponMesh;
	
	//碰撞box
	UPROPERTY(VisibleAnywhere,Category="Weapons")
	UBoxComponent* WeaponCollisionBox;
};
