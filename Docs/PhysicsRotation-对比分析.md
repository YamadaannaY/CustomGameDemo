# 原生 PhysicsRotation 与定制转向的对比分析

> 对象：`UExtraGameMovementComponent::PhysicsRotation` 对比引擎的 `UCharacterMovementComponent::PhysicsRotation`（UE 5.6）。

---

## 0. 结论速览

| 维度 | 原生 `PhysicsRotation` | 优化版本 |
|---|---|---|
| 目标朝向 | `Acceleration` 方向（瞬时跳变） | 输入方向 → 先落进 `LastRotationTarget`，再经第一层匀速逼近 |
| 逼近方式 | **恒定角速度**（`FixedTurn`），单次插值 | **两段插值**：第一段匀速、第二段指数 |
| 可调参数 | 只有 `RotationRate.Yaw` 一个 | `RotationRateCurve`（按速度档）+ 相机转速放大 + 总角差增幅 + 第一段速率 + 空中系数 |
| 起手/收尾 | 无法独立调，起步即满速、到位硬停 | 角加速度被第一段限幅，收尾指数缓出 |
| 相机联动 | 无 | 相机 yaw 速率 → 转向速率倍率（跟手） |
| 松手瞬间 | `Accel≈0` → 保持当前朝向，停在半途 | 记录意图方向，松手后继续收敛（`FallbackRotationSpeed`） |
| Montage 期间 | 只有带根运动的 Montage 才整体跳过 | 任何 Montage 都让位给 MotionWarping |
| 空中 | 与地面同一套速率 | 复用基类，只把 `RotationRate.Yaw` 乘 `AirRotationScale` |
| 服务端朝向 | 服务端自己算一份，与客户端有偏差 | 客户端朝向随 MoveData 上行，服务端照抄 |
| 代价 | 手感硬、不可配 | 帧间状态多、参数多、新增 MoveData 带宽、服务端信任客户端 yaw |

一句话：**原生是"恒定角速度追一个会瞬跳的目标"，优化版本是"把目标本身的跳变也过滤掉，再用指数逼近"**——区别不在插值函数，而在**被插值的目标是否是阶梯信号**。

---

## 1. 原生实现拆解

### 1.1 调用位置与时序

`UCharacterMovementComponent::PerformMovement` 末尾（引擎 `:2942`）：

```cpp
if (bAllowPhysicsRotationDuringAnimRootMotion || !HasAnimRootMotion())
{
    PhysicsRotation(DeltaSeconds);
}
```

三个时序事实决定了它的行为边界：

1. **在 `StartNewPhysics` 之后**（`:2932`）：位置/碰撞已经解算完，旋转不参与碰撞，`MoveUpdatedComponent` 用 `bSweep=false`。
2. **在动画根运动旋转之前**（`:2948` 起）：带根运动的 Montage 会在这之后再把旋转乘上去。
3. **只在本地控制的角色上跑**：服务端对远程玩家的角色走 `ServerAutonomousProxyTick`（`:1715-1730`），**不在 Tick 里做 PerformMovement**；它只会在 `ServerMove_PerformMovement` → `MoveAutonomous` → `PerformMovement` 里顺带跑一次 `PhysicsRotation`。

### 1.2 逐段拆解

```cpp
void UCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
    // ① 开关与前置于数据
    if (!(bOrientRotationToMovement || bUseControllerDesiredRotation)) return;
    if (!HasValidData() || (!CharacterOwner->GetController() && !bRunPhysicsWithNoController)) return;

    // ② 本帧允许转多少度
    DeltaRot = GetDeltaRotation(DeltaTime);   // 每轴 RotationRate*dt，上限 360；负值=360（瞬转）

    // ③ 目标朝向
    if (bOrientRotationToMovement)          DesiredRotation = ComputeOrientToMovementRotation(...);
    else if (bUseControllerDesiredRotation) DesiredRotation = Controller->GetDesiredRotation();

    // ④ 保持竖直（地面/下落）：Pitch=Roll=0，Yaw 归一化；自定义重力走 gravity-relative 空间
    if (ShouldRemainVertical()) { ... }

    // ⑤ 逐轴逼近
    DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);

    // ⑥ 落地为组件的旋转
    MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}
```

关键在 ③ 和 ⑤：

**③ 目标的来源（`:6431`）**

```cpp
if (Acceleration.SizeSquared() < KINDA_SMALL_NUMBER)
{
    if (bHasRequestedVelocity && RequestedVelocity.SizeSquared() > KINDA_SMALL_NUMBER)
        return RequestedVelocity.GetSafeNormal().Rotation();   // AI 寻路
    return CurrentRotation;                                    // 没有加速度 → 不转
}
return Acceleration.GetSafeNormal().Rotation();
```

`Acceleration` 由输入层每帧重写，**输入方向一变它就整帧跳变**（90°、180° 都是瞬跳）。没有加速度时直接返回当前朝向，所以"转到一半松手 → 停在半途"是**设计如此**，不是 bug。

**⑤ 逼近函数（`FMath::FixedTurn`）**

```cpp
if (InDeltaRate == 0)   return ClampAxis(InCurrent);      // 速率 0：完全不转
if (InDeltaRate >= 360) return ClampAxis(InDesired);      // 速率 ≥360/dt：直接对齐
// 否则：向 Desired 移动"最多 InDeltaRate"，且夹在 Desired 上（不越过）
```

`GetAxisDeltaRotation`（`:6420`）给的是 `RotationRate.Yaw * DeltaTime`（度/帧，上限 360；`RotationRate < 0` 直接给 360）。

### 1.3 数学性质：角速度是方波

设剩余角差为 `e`，则每帧转动

```
Δθ = sign(e) · min(Rate·dt, |e|)
```

即**角速度恒定**，只有"满速"和"0"两档：

```
ω(t) = Rate            e ≠ 0
ω(t) = 0               e = 0
```

- 起手：目标瞬跳 → `e` 一步到 90°/180° → 角速度**当帧跳变**到满值（角加速度无穷大）。
- 收尾：一路满速，到 `e` 小于一帧步长时当帧夹到目标 → 角速度**当帧归零**（硬刹）。

对比第二段用的 `FMath::RInterpTo`：它的 Alpha 是 `Clamp(速率 × dt, 0, 1)`、按**距离**缩放，所以"起手快、收尾缓"；而 `FixedTurn` 是"按角速率"缩放，不存在缓出。两者不是同一种曲线。

---

## 2. 原生在动作游戏里的七个问题

### 2.1 目标阶跃 → 角速度阶跃，起步是硬拐点

输入从"前"切到"右"，`Acceleration` 当帧从 +X 变 +Y。角色角速度从 0 直接跳到 540°/s。玩家看到的是"瞬间开始甩"而不是"发力再甩"，在 60fps 下尤其明显。想要柔化，唯一的旋钮是调小 `RotationRate`——但那会同时让长距离转身变慢。

### 2.2 一个参数同时决定两件事

`RotationRate.Yaw` 既是起手响应，也是收敛速度：

- 调大（540+）：起手干脆，但到位是硬停，且 180° 掉头会在 0.33s 内"刷"地转完，缺乏重量感。
- 调小（180）：起手发肉，稍长的转身像慢动作。

原生没有第二个自由度，这是结构性的。

### 2.3 到位没有缓出

`FixedTurn` 夹到目标即止。旋转末段仍是满速，最后一帧角度跳变归零。低帧率下能看到"最后一跳"，高帧率下能感到"刹住"。

### 2.4 松手就停在半途

`ComputeOrientToMovementRotation` 在 `Acceleration≈0` 时返回 `CurrentRotation`。于是：

- 玩家在转向过程中松开摇杆 → 角色**停在转到一半的朝向**，武器/身体停在半侧。
- 快速点按（<0.2s）后松手 → 朝向还差几十度就定住。

对依赖根运动的停步/急停尤其致命：这类 Montage 是按"松手瞬间的意图朝向"选左右和播根运动的，身体朝向若停在半途，Montage 的根位移方向与角色朝向会打架。

### 2.5 目标用"加速度方向"，不是"输入方向"

这是同一件事的两面：

- 180° 掉头时，`Acceleration` 立刻反向，但 `Velocity` 要先刹车过零才掉头。角色会"先原地转身、再往回跑"，转向与位移不同步。
- 反过来，缓变向时 `Acceleration` 已指向新方向而 `Velocity` 还在旧方向，原生追的是前者——这点是对的。

问题在于**"在动"的判据**：原生只看 `Acceleration` 是否为零，不看速度。松手后残余速度还在滑行，朝向却已经放弃更新。优化方案保留"加速度"作为意图判据，另外用 `FallbackRotationSpeed` 兜底，让松手后的滑行段继续收敛。

### 2.6 没有相机联动，跟手性无从谈起

`RotationRate` 是常量。第三人称动作游戏里，"甩镜头 180° 后角色多快跟上"是手感的核心变量，原生完全没有这个输入。

### 2.7 网络：服务端算出的朝向和客户端不是同一个

服务端对远程角色每次 `ServerMove_PerformMovement` 都会跑一遍 `MoveAutonomous → PerformMovement → PhysicsRotation`：

- 服务端用**服务端时间步**推进（`GetServerMoveDeltaTime`），客户端用本地 `DeltaTime`，两端每帧角度增量不同。
- 服务端自己的 `Acceleration` 来自 move data，但其 `Velocity` 与客户端预测结果有微差。
- 结果：**服务端胶囊朝向与客户端画面朝向存在持续偏差**。

而服务端要用这个朝向做的事全是权威判定：

- `ANS_WeaponTrace` 的 Socket 位置（只服务端结算）；
- `AN_AreaCheck` 的圆心与朝向；
- `ULockOnComponent` 的目标与 `MotionWarping` 的 warp target；
- `AbilitySystemComponent` 上的方向性判定。

偏差直接表现为"画面里打中了，服务端没中"或反之。这是网络层必须单独同步朝向的根本原因。

补充一个引擎事实，说明"服务端为什么不自己纠正"：响应包里的旋转由 `bHasRotation = CharacterMovement.ShouldCorrectRotation()` 决定（`CharacterMovementComponent.cpp:10665`），而该函数在基类恒返回 `false`（`CharacterMovementComponent.h:1104`）。原生从不因为朝向不一致去纠正客户端——它默认"朝向不是权威数据"。到了动作游戏里，朝向恰恰是权威数据。

---

## 3. 实现：从输入到应用，一步步复现

### 第 0 步：先看清链路

一次转向按执行顺序拆开是这样。引擎在第 4 步只用固定速率转一次，所以前 3 步的跳变都会原样传到第 5 步。

| 步 | 数据 | 引擎的做法 |
|---|---|---|
| 1 输入 | EnhancedInput → AddMovementInput → `Acceleration` | 同 |
| 2 意图方向 | `Acceleration` 的方向 | 直接用，逐帧可变，松手即归零 |
| 3 中间目标 | 没有这一环 | 无 |
| 4 角色旋转 | 当前朝向 → 目标朝向 | `FixedTurn`，恒定角速度 |
| 5 写组件 | `MoveUpdatedComponent` | 同 |
| 6 上行 | `SavedRotation` → 服务端 | 服务端自己再算一遍 |

下面六步严格按这条链路加东西。每一步都是一个可编译、可运行的完整状态：改完跑一次，确认现象之后再进下一步。

### 第 1 步：搭骨架，先让角色照常转向

后续每一步都要有个落点：引擎的 `PhysicsRotation` 改不了，只能在 `UCharacterMovementComponent` 的子类里覆盖。这一步只搭结构，行为要和原生一模一样，方便后面逐步替换时对比。

**头文件 `ExtraGameMovementComponent.h`**，在类里加这些声明（已有的跳过）：

```cpp
public:
	virtual void BeginPlay() override;
	virtual void PhysicsRotation(float DeltaTime) override;

	/** 总开关：false 时整套逻辑交回基类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Character Movement (Rotation)")
	bool bUseCustomPhysicsRotation = true;

	/** 空中转向系数，乘在基类使用的 RotationRate.Yaw 上 */
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.01", ClampMax="1.0"))
	float AirRotationScale = 0.3f;

	/** 第一段插值速率（度/秒） */
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float TargetRotationInterpSpeed = 800.f;

	/** 第二段插值的速率曲线：横轴 0..3 映射速度，纵轴收敛系数（1/秒） */
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)")
	TObjectPtr<UCurveFloat> RotationRateCurve;

	/** 相机甩镜速度达到该值时倍率封顶（度/秒） */
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="0.0"))
	float MaxCameraYawRate = 300.f;

	/** 倍率上限 */
	UPROPERTY(EditDefaultsOnly, Category="Character Movement (Rotation)", meta=(ClampMin="1.0"))
	float MaxCameraYawRateMultiplier = 3.f;

	/** 三档速度，用于把当前速度映射到 0..3 的曲线横轴 */
	void SetGaitSpeeds(float InWalk, float InRun, float InSprint);

	/** 当前速度映射到 0..3：0 停，1 走，2 跑，3 冲刺 */
	float GetMappedSpeed() const;

private:
	void  CustomPhysicsRotation(float DeltaTime);
	void  SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed, float ActorInterpSpeed, float DeltaTime);
	float CalculateGroundedRotationRate() const;
	float GetCameraYawRate() const { return CachedCameraYawRate; }

	float GaitWalkSpeed   = 250.f;
	float GaitRunSpeed    = 600.f;
	float GaitSprintSpeed = 800.f;

	FRotator TargetRotation     = FRotator::ZeroRotator;	// 被平滑的中间目标
	FRotator LastRotationTarget = FRotator::ZeroRotator;	// 最后一次有效输入方向

	float LastCameraYaw       = 0.f;
	float CachedCameraYawRate = 0.f;	// 度/秒
```

**源文件 `ExtraGameMovementComponent.cpp`**，先写 `BeginPlay` 和 `PhysicsRotation` 两个完整函数：

```cpp
void UExtraGameMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (CharacterOwner)
	{
		TargetRotation     = FRotator(0.f, CharacterOwner->GetActorRotation().Yaw, 0.f);
		LastRotationTarget = TargetRotation;
	}

	if (const AController* OwnerController = CharacterOwner ? CharacterOwner->GetController() : nullptr)
	{
		LastCameraYaw = OwnerController->GetControlRotation().Yaw;
	}
}

void UExtraGameMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!HasValidData() || !CharacterOwner)
	{
		return;
	}

	// 开关：false 时整套逻辑交回基类
	if (!bUseCustomPhysicsRotation)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	// 让位：有 Montage 在播时朝向由 MotionWarping / 根运动接管。
	// 必须把中间目标吸附到当前朝向，否则退出时会回头补转一整段
	const UAnimInstance* AnimInst = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const bool bAnimDrivingRotation = AnimInst && AnimInst->IsAnyMontagePlaying();

	if (bAnimDrivingRotation)
	{
		TargetRotation     = FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
		LastRotationTarget = TargetRotation;
		return;
	}

	// 地面转向：交给自定义函数
	if (IsMovingOnGround() && bOrientRotationToMovement)
	{
		CustomPhysicsRotation(DeltaTime);
		return;
	}

	// 空中只缩放速率，算法仍交基类，自定义重力与非竖直朝向都保留
	if (IsFalling())
	{
		TGuardValue<decltype(RotationRate.Yaw)> AirRateGuard(
			RotationRate.Yaw, RotationRate.Yaw * AirRotationScale);
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	// 游泳 / 飞行 / 其它模式
	Super::PhysicsRotation(DeltaTime);
}
```

`CustomPhysicsRotation` 这个阶段先原样转给基类，所以行为和原生一致：

```cpp
void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	Super::PhysicsRotation(DeltaTime);	// 占位，第 3 步替换
}
```

编译运行，确认角色走动、转身和改动前完全一样。服务端的朝向同步（`bSuppressServerRotation`）不在本节范围，见第 7 节。

### 第 2 步：把输入方向缓存下来

解决 2.5：引擎的目标是 `Acceleration` 的瞬时方向，输入一变就整帧跳变；松手后它归零，转向立刻停止，角色停在转到一半的朝向。

整段替换 `PhysicsRotation`，只在地面分支里多三行：

```cpp
void UExtraGameMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!HasValidData() || !CharacterOwner)
	{
		return;
	}

	if (!bUseCustomPhysicsRotation)
	{
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	const UAnimInstance* AnimInst = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const bool bAnimDrivingRotation = AnimInst && AnimInst->IsAnyMontagePlaying();

	if (bAnimDrivingRotation)
	{
		TargetRotation     = FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
		LastRotationTarget = TargetRotation;
		return;
	}

	if (IsMovingOnGround() && bOrientRotationToMovement)
	{
		// 记输入方向，不记速度方向：180 度掉头时速度要先刹车过零才反向，
		// 短促点按时速度也还没建起来，跟速度会慢半拍
		if (!Acceleration.IsNearlyZero(0.001f))
		{
			LastRotationTarget = FRotator(0.f, Acceleration.ToOrientationRotator().Yaw, 0.f);
		}

		CustomPhysicsRotation(DeltaTime);
		return;
	}

	if (IsFalling())
	{
		TGuardValue<decltype(RotationRate.Yaw)> AirRateGuard(
			RotationRate.Yaw, RotationRate.Yaw * AirRotationScale);
		Super::PhysicsRotation(DeltaTime);
		return;
	}

	Super::PhysicsRotation(DeltaTime);
}
```

判断只看 `Acceleration` 就够。它每帧由 `ConsumeInputVector()` 重算，松手当帧归零，有加速度就等于有输入；惯性只体现在 `Velocity` 上。不要在这里加速度门槛，静止起步时速度必然是 0，门槛会挡住最需要响应的那几帧。

此时 `CustomPhysicsRotation` 还是占位版，角色仍在按原生逻辑转，只是多存了一个 `LastRotationTarget`。跑一下确认没有异常。

### 第 3 步：两段插值

这一步同时解决 2.1 和 2.3。

2.1 是因为目标方向在跳：直接把 `LastRotationTarget` 当目标，角速度会在输入变化的那一帧从 0 跳到满值。给目标本身限速，阶跃就变成斜坡，角加速度有了上限。

2.3 是因为引擎恒速：`FixedTurn` 一路满速，到位当帧归零。让角色按误差比例追，误差大就快、误差小就慢，收尾自然。

**整段替换 `CustomPhysicsRotation`**，并**新增** `SmoothCharacterRotation`：

```cpp
void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	// 速率先用基线值，第 4 步再换成曲线和相机倍率
	SmoothCharacterRotation(LastRotationTarget, TargetRotationInterpSpeed, RotationRate.Yaw, DeltaTime);
}

void UExtraGameMovementComponent::SmoothCharacterRotation(const FRotator& Target, float TargetInterpSpeed,
                                                          float ActorInterpSpeed, float DeltaTime)
{
	// 第一段：中间目标匀速逼近意图方向，每帧最多走 TargetInterpSpeed * DeltaTime
	TargetRotation = FMath::RInterpConstantTo(TargetRotation, Target, DeltaTime, TargetInterpSpeed);

	// 第二段：角色按误差比例收敛到中间目标
	FRotator DesiredRotation = FMath::RInterpTo(
		UpdatedComponent->GetComponentRotation(), TargetRotation, DeltaTime, ActorInterpSpeed);

	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll  = 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, false);
}
```

这一段的两个参数单位不同：前者是度/秒，直接决定最多多快进入新方向，90 度约 0.11 秒走完；后者是 1/秒，只决定收敛形状。两者都必须为正，两个插值函数在速率小于等于 0 时都会直接返回目标值，一次瞬转就盖掉整个平滑过程。

跑一下，重点看两件事：变向时起步是否变软，到位时是否还会硬停。

### 第 4 步：让速率随速度和视角变化

解决 2.2 和 2.6：常量速率没法同时适配走跑冲刺，也跟不上甩镜头的节奏。

**新增** `MapRangeClamped`、`GetMappedSpeed`、`CalculateGroundedRotationRate` 三个函数：

```cpp
namespace
{
	// 把 Value 从 In 区间映射到 Out 区间并夹紧
	float MapRangeClamped(float Value, float InMin, float InMax, float OutMin, float OutMax)
	{
		if (InMax <= InMin)
		{
			return OutMin;
		}
		const float T = FMath::Clamp((Value - InMin) / (InMax - InMin), 0.f, 1.f);
		return FMath::Lerp(OutMin, OutMax, T);
	}
}

float UExtraGameMovementComponent::GetMappedSpeed() const
{
	const float Speed = Velocity.Size2D();

	if (Speed > GaitRunSpeed)
	{
		return MapRangeClamped(Speed, GaitRunSpeed, GaitSprintSpeed, 2.0f, 3.0f);
	}
	if (Speed > GaitWalkSpeed)
	{
		return MapRangeClamped(Speed, GaitWalkSpeed, GaitRunSpeed, 1.0f, 2.0f);
	}
	return MapRangeClamped(Speed, 0.0f, GaitWalkSpeed, 0.0f, 1.0f);
}

float UExtraGameMovementComponent::CalculateGroundedRotationRate() const
{
	float BaseRate = RotationRate.Yaw;
	if (RotationRateCurve)
	{
		const float CurveValue = RotationRateCurve->GetFloatValue(GetMappedSpeed());
		if (CurveValue > 0.f)
		{
			BaseRate = CurveValue;
		}
	}

	if (MaxCameraYawRate <= 0.f)
	{
		return BaseRate;
	}

	// 相机甩得越快，角色跟得越快
	const float CameraYawRateScale = MapRangeClamped(
		GetCameraYawRate(), 0.f, MaxCameraYawRate, 1.f, FMath::Max(MaxCameraYawRateMultiplier, 1.f));

	return BaseRate * CameraYawRateScale;
}
```

**整段替换 `CustomPhysicsRotation`**，加上相机速率，并把速率换成 `CalculateGroundedRotationRate()`：

```cpp
void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	CachedCameraYawRate = 0.f;
	if (const AController* OwnerController = CharacterOwner->GetController())
	{
		const float CurrentCameraYaw = OwnerController->GetControlRotation().Yaw;
		CachedCameraYawRate = FMath::Abs(
			FMath::FindDeltaAngleDegrees(LastCameraYaw, CurrentCameraYaw)
			/ FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		LastCameraYaw = CurrentCameraYaw;
	}

	SmoothCharacterRotation(LastRotationTarget, TargetRotationInterpSpeed,
		CalculateGroundedRotationRate(), DeltaTime);
}
```

速度映射用归一化横轴的原因很简单：改走跑冲刺速度只改 `GaitWalkSpeed` 这几个成员，曲线不用重画。

相机速率有一个必须注意的点：`LastCameraYaw` 要在地面分支之外也更新。它现在只在 `CustomPhysicsRotation` 里更新，而空中和 Montage 都不会进这个函数，回到地面那一帧会把整段中断期间的转角压进一帧，倍率瞬间打满。稳妥的做法是在 `PhysicsRotation` 的让位分支和空中分支里也写一行 `LastCameraYaw = ...`。

跑一下，配好三档速度和曲线，确认走、跑、冲刺的转向手感有明显差异，甩镜头时角色跟得上。

### 第 5 步：松手后把朝向收完

解决 2.4：引擎在 `Acceleration` 归零后就不再更新朝向，角色会停在转到一半的朝向。现在有了缓存，只要再加一个兜底条件就能收完。

**新增**匿名命名空间里的常量，并**整段替换 `CustomPhysicsRotation`**：

```cpp
namespace
{
	// 已松手但速度仍高于此值时，继续把朝向收完
	constexpr float FallbackRotationSpeed = 10.f;
}

void UExtraGameMovementComponent::CustomPhysicsRotation(float DeltaTime)
{
	CachedCameraYawRate = 0.f;
	if (const AController* OwnerController = CharacterOwner->GetController())
	{
		const float CurrentCameraYaw = OwnerController->GetControlRotation().Yaw;
		CachedCameraYawRate = FMath::Abs(
			FMath::FindDeltaAngleDegrees(LastCameraYaw, CurrentCameraYaw)
			/ FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		LastCameraYaw = CurrentCameraYaw;
	}

	const float Speed = Velocity.Size2D();
	const bool bHasMovementInput = !Acceleration.IsNearlyZero(0.001f);

	// 有输入就转，含原地转身；没有输入但还在滑行时也继续收敛
	const bool bShouldRotate = bHasMovementInput || Speed > FallbackRotationSpeed;

	if (!bShouldRotate)
	{
		return;
	}

	SmoothCharacterRotation(LastRotationTarget, TargetRotationInterpSpeed,
		CalculateGroundedRotationRate(), DeltaTime);
}
```

跑一下，重点看松手：以前会停在半侧，现在应该边减速边把朝向收正，和减速同时结束。

### 第 6 步：把朝向传给服务端

原因：服务端要用朝向做权威判定（武器 Trace、范围判定、锁定），但它拿不到客户端的输入与相机数据，复现不出前五步的两段插值。两端各算一遍必然有偏差，只能客户端算完、服务端照抄。

这条链路在第 0 步的表里是第 6 行，分两段：

```
客户端每帧
  PerformMovement（内含 PhysicsRotation）
  → FSavedMove_Character::PostUpdate 写 SavedRotation = GetActorRotation()
  → 发包时 ClientFillNetworkMoveData 把 SavedRotation 装进 MoveData
  → Serialize：1 bit 有效位 + FRotator 量化

服务端收包
  ServerMove → ServerMove_PerformMovement
  → 抑制自己的 PhysicsRotation
  → Super 回放这次移动（位置/速度仍由服务端算）
  → 时间戳匹配时才 SetActorRotation(ClientRotation)
```

`SavedRotation` 由引擎在 `PerformMovement` 之后写入，所以它已经是这一帧结束时的胶囊朝向，包含动画根运动带来的旋转，不用自己再算一遍。

**6.1 自定义 MoveData**（新文件 `ExtraCharacterNetworkMoveData.h`）

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

struct FExtraCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
	FRotator ClientRotation = FRotator::ZeroRotator;
	bool bHasClientRotation = false;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove,
		ENetworkMoveType MoveType) override
	{
		FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);

		// SavedRotation 在 PerformMovement 之后写入，就是这一帧移动结束时的胶囊朝向
		ClientRotation     = ClientMove.SavedRotation;
		bHasClientRotation = true;
	}

	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar,
		UPackageMap* PackageMap, ENetworkMoveType MoveType) override
	{
		if (!FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
		{
			return false;
		}

		// 先写 1 bit 有效位，再用 FRotator 的量化序列化
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
```

**6.2 容器：New / Pending / Old 三个槽位都要换**

```cpp
struct FExtraCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FExtraCharacterNetworkMoveDataContainer()
	{
		NewMoveData     = &CustomMoveData[0];
		PendingMoveData = &CustomMoveData[1];
		OldMoveData     = &CustomMoveData[2];
	}

	FExtraCharacterNetworkMoveData CustomMoveData[3];
};
```

容器必须是组件的持久成员：`SetNetworkMoveDataContainer` 只存指针，临时对象会悬空。

```cpp
// ExtraGameMovementComponent.h
#include "ExtraCharacterNetworkMoveData.h"

	// 只在 ServerMove_PerformMovement 调用 Super 期间为 true
	bool bSuppressServerRotation = false;

	// 自定义 MoveData 容器，必须在组件生命周期内持久存在
	FExtraCharacterNetworkMoveDataContainer CustomNetworkMoveDataContainer;

// ExtraGameMovementComponent.cpp 构造函数
UExtraGameMovementComponent::UExtraGameMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 用自定义容器替换默认容器，让 ServerMove 包携带客户端朝向
	SetNetworkMoveDataContainer(CustomNetworkMoveDataContainer);
}
```

**6.3 服务端：抑制自己的旋转，再照抄客户端朝向**

```cpp
void UExtraGameMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	// 容器不是自己的类型时，完全走原生逻辑
	if (&GetNetworkMoveDataContainer() != &CustomNetworkMoveDataContainer)
	{
		Super::ServerMove_PerformMovement(MoveData);
		return;
	}

	const FExtraCharacterNetworkMoveData& Extra =
		static_cast<const FExtraCharacterNetworkMoveData&>(MoveData);

	// Super 期间抑制服务端自己的 PhysicsRotation，避免它把客户端朝向覆盖掉
	TGuardValue<bool> SuppressGuard(bSuppressServerRotation, true);

	Super::ServerMove_PerformMovement(MoveData);

	// 一批包里 Old / Pending / New 都会走这里，只有被真正接受的那个 move
	// 时间戳才等于 CurrentClientTimeStamp；过期/重复的不覆盖
	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	if (Extra.bHasClientRotation && CharacterOwner && ServerData &&
		ServerData->CurrentClientTimeStamp == MoveData.TimeStamp)
	{
		CharacterOwner->SetActorRotation(Extra.ClientRotation);
	}
}
```

**6.4 给第 1 步的 `PhysicsRotation` 补一个出口**

放在函数最前面，和其它出口并列：

```cpp
	// 服务端上的远程客户端：朝向由客户端主导。
	// bSuppressServerRotation 只在处理该玩家 MoveData 期间为 true
	if (CharacterOwner->GetLocalRole() == ROLE_Authority
		&& CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy
		&& bSuppressServerRotation)
	{
		return;
	}
```

到此，客户端算出来的朝向会原样出现在服务端，模拟代理通过 `ReplicatedMovement` 看到的也是同一个朝向。

### 第 7 步：收尾与验证

改动落在五个函数和一处 override 上：

| 函数 | 职责 |
|---|---|
| `PhysicsRotation` | 选路：开关、让位、地面、空中、其余 |
| `CustomPhysicsRotation` | 算相机速率，判断要不要转，调插值 |
| `SmoothCharacterRotation` | 两段插值，写组件 |
| `CalculateGroundedRotationRate` | 速度曲线 × 相机倍率 |
| `ServerMove_PerformMovement` | 服务端抑制自身旋转，回放后照抄客户端朝向 |

参数与建议值：

| 参数 | 单位 | 建议 | 作用 |
|---|---|---|---|
| `TargetRotationInterpSpeed` | 度/秒 | 600~1200 | 进入新方向多快，90 度约 0.1 秒 |
| `ActorInterpSpeed`（曲线纵轴） | 1/秒 | 6~20 | 收尾快慢；超过 60 会每帧直接对齐 |
| `FallbackRotationSpeed` | cm/s | 8~15 | 松手后继续收敛的门槛 |
| `MaxCameraYawRate` | 度/秒 | 200~400 | 倍率达到上限所需的甩镜速度 |
| `MaxCameraYawRateMultiplier` | 倍 | 2~3.5 | 跟手上限 |
| `AirRotationScale` | 倍 | 0.2~0.5 | 空中速率系数，不能为 0 |
| `GaitWalk/Run/SprintSpeed` | cm/s | 与实际速度一致 | 曲线横轴的分段点 |

五个容易踩的地方：

1. 两个速率单位不同。前者是度/秒，后者是 1/秒，`RInterpTo` 内部算的是 `Alpha = 速率 * DeltaTime`。把 540 这类度/秒的数值填进曲线，60fps 下每帧都会被钳成 1，两段插值退化成一段。
2. `TargetRotation`、`LastRotationTarget`、`LastCameraYaw` 都是跨帧状态。客户端预测回滚重放 move 时它们不会跟着回滚，重放出来的朝向可能和首次预测不同。
3. 每帧都 `MoveUpdatedComponent`，哪怕已经对齐，也会多一次 `UpdateComponentToWorld`。加一个 `DesiredRotation.Equals(当前旋转, 1e-3f)` 的短路更省。
4. 开关 `bUseCustomPhysicsRotation` 同时切掉了让位和接管两件事。做 A/B 时只动它，量到的是整套方案；想单独比较两段插值与原生恒速，需要另加一个只切插值的开关。
5. 服务端覆盖朝向必须带时间戳过滤（`CurrentClientTimeStamp == MoveData.TimeStamp`）。一批包里 Old / Pending / New 会依次进入这个函数，不过滤就会用旧 move 的朝向盖掉最新的。另外 `SavedRotation` 只在 `PerformMovement` 之后才写，别在 `PhysicsRotation` 里读它——读到的是上一次 move 的朝向。

---

## 4. 优化前后逐项对照

| # | 环节 | 优化前 | 优化后 | 解决什么 |
|---|---|---|---|---|
| 1 | 目标来源 | `Acceleration` 方向 | 输入方向缓存进 `LastRotationTarget` | 松手不再停在半途；意图先于速度 |
| 2 | 目标刷新条件 | `Accel ≠ 0` | 只看 `Accel ≠ 0`，不加速度门槛 | 静止起步不再迟滞 2~4 帧 |
| 3 | 兜底收敛 | 松手即停止更新 | 速度高于 `FallbackRotationSpeed` 时继续收敛 | 松手不再停在半途 |
| 4 | 逼近曲线 | `FixedTurn` 恒速 | 第一段 `RInterpConstantTo` + 第二段 `RInterpTo` | 角加速度有界 + 收尾缓出 |
| 5 | 速率 | `RotationRate.Yaw` 常量 | `RotationRateCurve(MappedSpeed)` | 走/跑/冲刺不同转向手感 |
| 6 | 速率调制 | 无 | × 相机 yaw 速率倍率 | 跟手 |
| 7 | 空中 | 与地面同速率 | × `AirRotationScale`(0.3) | 空中更钝，落地更好控 |
| 8 | Montage | 只有带根运动才跳过 | 任何 Montage 都让位并把目标吸附到当前 | 不与 MotionWarping 抢朝向 |
| 9 | 服务端朝向 | 服务端自算 | 客户端 `SavedRotation` 上行覆盖 | 消除两端朝向偏差，权威判定与画面一致 |
| 10 | 可对比性 | — | `bUseCustomPhysicsRotation` 开关 | 一键回到原生做 A/B |

---

## 5. 优势

1. **手感可控且解耦**：起手响应（第一段的 800）、收敛形状（第二段的 λ）、速度档差异（曲线）、跟手程度（相机倍率）是四个独立旋钮。原生死死绑在一个 `RotationRate` 上。
2. **有加速度过渡**：输入切换不再产生角速度阶跃，观感上是"发力→转向→收住"，接近主机动作游戏的手感。
3. **意图优先于速度**：用输入方向而不是速度方向，急变向和短促点按都更跟手，也顺带解决了原生"松手停在半途"。
4. **让位机制清晰**：Montage 期间朝向完全交给 MotionWarping/根运动，且交接时目标吸附当前朝向，没有"回头补转"的跳帧。
5. **保留引擎路径**：空中/游泳/飞行没有重写，只缩放参数。自定义重力、非竖直朝向这些边缘情况仍然正确——这是很多手写 `PhysicsRotation` 会踩丢的。
6. **网络朝向一致**：服务端拿到的是客户端预测后的最终朝向，武器 Trace / 范围判定 / 锁定与画面一致；这是网络侧最能被玩家感知的收益（近战判定不再"看着中了没中"）。
7. **可回退**：一个布尔开关回到原生，便于线上出问题时的隔离。

---

## 6. 代价与风险

### 6.1 实现级隐患（按严重度排）

**① `LastCameraYaw` 只在地面分支里更新。**
让位分支（有 Montage）和空中分支都不会调用 `CustomPhysicsRotation`，`LastCameraYaw` 于是停留在旧值。落地或 Montage 结束的首帧，`CachedCameraYawRate = |ΔYaw| / dt` 会把整个中断期间的相机位移全部算进一帧——**尖峰 → 倍率直接打满 → 角色当帧以 3 倍速转向**。

修法（任选）：在让位分支里也更新 `LastCameraYaw`（空中分支同理）；或给 `CachedCameraYawRate` 加一个上限（如 600°/s）；或对 `dt` 取 `Max(dt, 1/30)` 做平滑。

**② `IsAnyMontagePlaying()` 在 BlendOut 期间仍为 true。**
它等价于 `MontageInstances.Num() > 0`，处于淡出的 Montage 仍算"在播"。若项目里有停步、急停这类短 Montage，退出转向的那几帧会表现为"快速连点变向后，角色要等淡出完才开始转"。要精确就改成遍历目标 Montage 做 `Montage_IsPlaying`，或只认 `GetCurrentActiveMontage()`。

**③ 第二段的参数单位与曲线配置容易错位**（见第 5 步）。当曲线值落在 deg/s 量级时，第二段恒被钳到 1，两段插值退化成一段——代码看起来是两段，手感只有一段。建议在注释里写明"Alpha = 速率 × dt，纵轴应 ≤ 帧率"。

**④ 每帧 `MoveUpdatedComponent` 无条件调用。**
基类有 `CurrentRotation.Equals(DesiredRotation, 1e-3)` 的短路（`CharacterMovementComponent.cpp:6521`），原地不动时不进 `MoveUpdatedComponent`。这套实现只要 `bShouldRotate` 为真就调，即使已对齐。开销很小（无 sweep），但会每帧触发 `UpdateComponentToWorld`，大量角色同屏时不是零成本。

### 6.2 结构级取舍

**⑤ 帧间状态多，可复现性下降。**
`TargetRotation` / `LastRotationTarget` / `LastCameraYaw` / `CachedCameraYawRate` 都是跨帧状态。客户端预测回滚（`ClientUpdatePositionAfterServerUpdate` 重放 move）时，这些状态不会跟着回滚——重放得到的朝向可能与首次预测不同。若再用"服务端照抄客户端朝向"来对齐两端，等于**放弃了朝向的预测一致性校验**。

**⑥ 服务端完全信任客户端 yaw。**
`SetActorRotation(ClientRotation)` 是原样采纳，没有任何范围校验。恶意客户端可以上传任意朝向，绕过"背身不能命中"这类依赖朝向的判定。要加防护可以：限制单帧 yaw 变化上限（如 ≤ 90°/move），或对 `ClientRotation` 与"服务端上次朝向 + 允许速率 × dt"做 clamp。

**⑦ 只覆盖地面 + `bOrientRotationToMovement`。**
若需求变成"地面 + `bUseControllerDesiredRotation`"（比如瞄准姿态上身独立），地面分支不成立，会掉回原生单次插值，手感突然变化。启用这套方案时 `bUseControllerRotationYaw` 应为 false。

**⑧ 绕过自定义重力。**
`SmoothCharacterRotation` 里无条件 `Pitch = Roll = 0`，没有 `HasCustomGravity()` 的 gravity-relative 处理。标准 Z-up 重力下无差别，但若要做墙面行走或重力翻转，地面分支会强行把角色拉直。

**⑨ 带宽与容器替换。**
如果朝向要随 MoveData 上行，每个 move 多 1 bit + 一个 `FRotator`（约 3~4 字节量化），60Hz 下可忽略。真正的风险在容器替换：`SetNetworkMoveDataContainer` 只存指针，容器必须是组件的持久成员。容器内是 POD、不引用 UObject 时，普通成员即可；若将来往 MoveData 里加 UObject 指针，这个成员就必须加 `UPROPERTY` 或改由 GC 管理。

---

## 7. 三条链路

### 7.1 客户端（本地预测）

```
EnhancedInput → AExtraPlayerCharacter::Move()
   ├─ 无输入→有输入：ClearStopRequest / CancelStopMontage / Server_SetMoveInputState(true)
   ├─ 以 ControlRotation.Yaw 为基准合成输入方向
   ├─ CalculateTargetDelta()（供松手判定急停/转身）
   ├─ InputDirection = 单位输入方向; Server_SendInputDirection()
   └─ AddMovementInput(dir, mag) → Acceleration

CharacterMovementComponent::TickComponent
   └─ ControlledCharacterMove → ReplicateMoveToServer → PerformMovement
        ├─ StartNewPhysics → PhysWalking（MovementCurve 覆盖 GroundFriction）
        ├─ UExtraGameMovementComponent::PhysicsRotation
        │     ├─ 有 Montage → 目标吸附当前朝向，return
        │     └─ 地面 → CustomPhysicsRotation
        │            ├─ CachedCameraYawRate
        │            ├─ bShouldRotate 判定
        │            ├─ GroundedRate = CalculateGroundedRotationRate()
        │            └─ SmoothCharacterRotation(LastRotationTarget, 800, GroundedRate)
        │                  ├─ TargetRotation = RInterpConstantTo(..., 800°/s)
        │                  ├─ Desired = RInterpTo(current, TargetRotation, ActorInterpSpeed)
        │                  └─ MoveUpdatedComponent(0, Desired, false)
        └─ 之后 FSavedMove_Character::PostUpdate → SavedRotation = GetActorRotation()
```

`SavedRotation` 的写入时机很重要：它在 `PerformMovement` 全部走完（含根运动旋转）之后才写，所以上行的是**最终朝向**，不是中间值。

### 7.2 服务端（专用服务器上的远程玩家）

```
ServerMove RPC → ServerMove_Implementation
   └─ 按 Old / Pending / New 顺序回放：
        ServerMove_PerformMovement(MoveData)                    // 自定义 override
          ├─ 容器校验
          ├─ TGuardValue(bSuppressServerRotation = true)
          ├─ Super::ServerMove_PerformMovement
          │     ├─ VerifyClientTimeStamp
          │     ├─ SetControlRotation(ClientControlRotation)
          │     ├─ MoveAutonomous → PerformMovement
          │     │     ├─ StartNewPhysics（位置/速度权威）
          │     │     └─ PhysicsRotation → 被守卫短路，不覆盖朝向
          │     └─ ServerMoveHandleClientError（位置偏差纠正）
          └─ 时间戳匹配 → SetActorRotation(ClientRotation)       // 朝向照抄
```

注意两点：

- 引擎对这类角色**没有独立 Tick 的 `PerformMovement`**：远程玩家的胶囊只在处理 move 时更新，所以客户端发多少 move，服务端就同步多少朝向，不存在"服务端自己漂移"。
- `bSuppressServerRotation` 的生效条件带 `RemoteRole == ROLE_AutonomousProxy`，所以 Listen Server 上**房主自己的角色**（LocalRole=Authority、RemoteRole=SimulatedProxy）不会被抑制，仍然走完整的本地两段插值——房主是本地玩家，朝向由自己算。

### 7.3 模拟代理（其他客户端看到的）

```
服务端 SetActorRotation
   └─ ReplicatedMovement 把位置+朝向下发给其他端
        └─ 客户端 SimulatedTick → 平滑 → 更新胶囊
             └─ PhysicsRotation 不参与（模拟代理不跑 PerformMovement）
```

也就是说，模拟端看到的朝向完全由服务端那次 `SetActorRotation` 决定——**这正是必须让服务端照抄客户端朝向的原因**：不然第三人称里别人看到的你，会和你自己屏幕上转的方向差一截。

---

## 8. 参数速查

| 参数 | 语义 | 建议 |
|---|---|---|
| `bUseCustomPhysicsRotation` | 总开关（同时切换让位与接管） | 线上保持 true；排查时临时 false |
| `TargetRotationInterpSpeed` | 第一段，**度/秒** | 600~1200；越大越跟手，越小越软 |
| `RotationRateCurve` | 第二段，横轴 MappedSpeed，纵轴是 **1/秒 的收敛比例** | 纵轴 6~15（60fps）；填 deg/s 会被钳成 1 |
| `FallbackRotationSpeed` | 松手后仍继续收敛的最低速度 | 10 左右 |
| `MaxCameraYawRate` | 相机倍率达到上限所需的甩镜速度 | 200~400 °/s |
| `MaxCameraYawRateMultiplier` | 倍率上限 | 2~3.5 |
| `AngleRateBoost` | 总角差增幅上限：180° 时第二层 λ 放大到 (1+该值) 倍 | 0.5~1.0；0 = 关闭 |
| `AngleRateBoostStart` / `AngleRateBoostFull` | 增幅起点 / 满额角差（度） | 20~30 / 150~180 |
| `MaxGroundedRotationRate` | 第二层 λ 的最终上限（1/秒），防 α 饱和 | 30~45；0 = 不限制 |
| `AirRotationScale` | 空中速率系数（乘在基类 `RotationRate.Yaw`） | 0.2~0.5，勿为 0 |
| `GaitWalkSpeed` / `GaitRunSpeed` / `GaitSprintSpeed` | 曲线横轴的分段点 | 与实际移动速度一致 |
| `StopBrakingDeceleration` | 停步窗口刹车（非转向，但与转向观感耦合） | 300~800 |

调参顺序建议：先定第一段（决定"进入新方向多快"）→ 再定曲线纵轴（决定"收得多稳"）→ 用相机倍率补跟手 → 用总角差增幅补大角度转身的滞后尾巴 → 空中系数单独试。

---

## 9. 若要继续改进（按性价比）

1. **修 ① 的相机速率尖峰**（一行：在让位分支更新 `LastCameraYaw`，或给速率加上限）。这是目前最可能被玩家感知的缺陷。
2. **修 ② 的 Montage BlendOut 判定**（改用 `GetCurrentActiveMontage` + 目标 Montage 白名单）。
3. **给 `SetActorRotation` 加朝向变化上限**，把"完全信任客户端"降级为"有限信任"。
4. **把第二段的曲线单位写进注释**，并在编辑器里核对一次实际值——这决定了两段插值是不是真的两段。
5. 若将来出现非 Z-up 重力，把 `SmoothCharacterRotation` 的 Pitch/Roll 清零改成 gravity-relative。