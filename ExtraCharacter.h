#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Character.h"
#include "ExtraCharacter.generated.h"

class UExtraAbilitySystemComponent;
class UExtraGameWeaponComponent;
class UExtraGameAttributeSet;
class UOverHeadStatsGauge;

UCLASS()
class EXTRACTGAMECHARACTER_API AExtraCharacter : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AExtraCharacter(const FObjectInitializer& ObjectInitializer);

	// ── IAbilitySystemInterface ────────────────────────────────
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// ── GAS 初始化（由 Controller OnPossess 调用）─────────────
	// 先调用 InitAbilityActorInfo，再调用 ASC 的 ServerSideInit
	virtual void ServerSideInit();

	// ── 组件访问器 ─────────────────────────────────────────────
	UFUNCTION(BlueprintPure, Category = "Weapon")
	UExtraGameWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	// GAS 是否已初始化（防止 OnPossess 重复调用）
	bool bGASInitialized = false;

	// 强制开启服务器→模拟端加速度复制：AnimBP 用 HasAcceleration() 判断移动状态，
	// 若不复制，模拟端 GetCurrentAcceleration() 退化为单位向量，移动过渡条件失真导致角色以 idle 移动。
	virtual bool ShouldReplicateAcceleration() const override { return true; }

	// 跳跃高度（cm）。BeginPlay 时按当前重力换算成 JumpZVelocity 写入移动组件，
	// 便于在编辑器里直接填目标高度而非初速度。默认 90 与引擎默认 JumpZVelocity=420 一致。
	UPROPERTY(EditDefaultsOnly, Category = "Character Movement (Jumping)", meta = (ClampMin = "0.0"))
	float JumpHeight = 90.f;
	
	/******************************* Team ***********************************/
	/** Assigns Team Agent to given TeamID 在Controller OnPossess时调用*/
	//Problem:为什么不可以在这里调用PickSkin函数，根据ID换Mesh?
	//Answer:这个函数只在服务端执行，如果在服务端调用，ID会复制给所有客户端，但是Mesh默认是不复制的，也就是不会复制给客户端。
	//换句话说，PickSkin确实执行了，但是客户端看不到，应该用OnRep，让客户端收到ID的那一刻根据ID改变Mesh
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;

	/** Retrieve team identifier in form of FGenericTeamId */
	virtual FGenericTeamId GetGenericTeamId() const override;

protected:
	virtual void BeginPlay() override;
	
	// ── GAS ────────────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UExtraAbilitySystemComponent> AbilitySystemComponent;

	// ── 武器系统 ───────────────────────────────────────────────
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UExtraGameWeaponComponent> WeaponComponent;
	
	/***********UI************/

	FTimerHandle HeadStatGaugeVisibilityUpdateTimerHandle;
	
	//Gauge可视组件
	UPROPERTY(VisibleAnywhere, Category="UI")
	class UWidgetComponent* OverHeadWidgetComponent;

	//头顶血条 Widget 类（蓝图里配置为 OverHeadStatsGauge 的蓝图子类）
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UOverHeadStatsGauge> OverHeadWidgetClass;

	//判断距离是否隐藏组件Timer的更新间隔
	UPROPERTY(EditDefaultsOnly, Category="UI")
	float HeadStatGaugeVisibilityUpdateGap = 3.f;

	//要判断距离的平方（以节省开方计算性能为目的）
	UPROPERTY(EditDefaultsOnly, Category="UI")
	float HeadStatGaugeVisibilityRangeSquared = 10000000.f;
	
	
	//配置Gauge可视组件，将Component转为配置好的GaugeWidget类，并为其绑定委托
	void ConfigureOverHeadStatusWidget();

	//客户端调用，Timer绑定的回调，对客户端的本地Actor调用，根据与本地客户端角色实例的距离判断是否要显示自己的OverheadUI
	void UpdateHeadGaugeVisibility() const ;

	//Death状态下在客户端调用，判断是否显示OverHeadWidget
	void SetStatusGaugeEnabled(bool bEnabled);
	
	bool IsLocallyControlledByPlayer() const 
	{
		//判断LocalController从而找到当前客户端的主Player
		return GetController() && GetController()->IsLocalPlayerController();
	}
	
	
private:
	UPROPERTY(ReplicatedUsing="OnRep_TeamID")
	FGenericTeamId TeamID;
	
	UFUNCTION()
	virtual void OnRep_TeamID();
};
