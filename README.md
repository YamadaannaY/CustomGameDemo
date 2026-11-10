# ExtractGameCharacter 技术架构文档

> 项目：UE5 GAS 动作战斗 Demo（对《鸣潮》角色「绯雪」技能组复刻）
> 用途：长期复习、技术文档
> 最后更新：2026-11-10

核心目录：

```
AI/            AI 角色（木桩）
AN/            AnimNotify / AnimNotifyState（动画时间线上的核心逻辑分发）
Camera/        战斗相机组件、曲线相机震动、调试分类
Debug/         Gameplay Debugger 分类
GAS/           ASC、GA 基类、技能实现、TargetData、GameplayCue
LockOn/        锁定组件
Projectile/    投射物基类、箭矢、剑气
UI/            HUD、血条、能量条、伤害飘字
WeaponSystem/  武器组 DataAsset、武器组件、属性集
```

---

## 1. 总览

### 1.1 技术栈

| 能力 | 实现                                            |
|---|-----------------------------------------------|
| 能力系统 | GAS(自定义 ASC / GA / GE / AttributeSet）         |
| 输入 | Enhanced Input                                |
| 移动 | 定制化 `UCharacterMovementComponent`             |
| 动画 | AnimBP + AnimNotify/NotifyState + MotionWarping |
| 相机 | SpringArm + 自定义请求栈组件 + CameraShake 曲线资产       |
| 网络 | DS 复制 + GAS 复制通道                              |
| 数据 | DataAsset（武器）、DataTable（属性）、Curve 资产（移动/步幅/震动）|

### 1.2 核心对象关系

```
APlayerController
  └─ OnPossess → AExtraCharacter::ServerSideInit()
       ├─ UExtraAbilitySystemComponent::ServerSideInit()
       │    ├─ 创建 UExtraGameAttributeSet，按 DT 初始化数值
       │    ├─ 应用 InitEffects
       │    └─ 授予 InnateAbilities / PassiveAbilities
       └─ UExtraGameWeaponComponent::OnASCInitialized()
            └─ EquipWeaponGroup(DefaultWeaponGroupTag)

AExtraPlayerCharacter : AExtraCharacter
  ├─ UExtraGameMovementComponent 自定义的CMC组件，优化移动转向逻辑
  ├─ UMotionWarpingComponent Montage的MW区间转向，用于追踪、锁定、穿透
  ├─ UCombatCameraComponent 战斗时依据AN/ANS启用的战斗镜头组件
  ├─ ULockOnComponent  锁定对象组件
  ├─ SpringArm + Camera 基础相机
  └─ WeaponComponent（继承自 AExtraCharacter）
```

### 1.3 关键设计原则

1. **服务端权威**：伤害、锁定、GA 授予、GE、轨迹扫描、范围判定只在服务端执行。
2. **表现各端本地生成**：武器 Mesh、Montage、相机、音频、飘字由各端依据复制状态自行产生。
3. **事件驱动**：GA 与动画之间不直接调用，统一通过 `GameplayEvent + TargetData` 通信。
4. **数据驱动**：攻击段、能量、镜头、音频概率、武器组、属性均在资产/面板配置，改数值不改代码。
5. **基类收敛通用机制**：伤害、能量、取消窗口、霸体、推力、重力、锁定追踪都由基类GA在PreActive中统一实现，子类只写差异化流程。

### 1.4 初始化时序（DS）

**服务端**

```
AController::OnPossess
 → AExtraCharacter::ServerSideInit            // bGASInitialized 防重入
    → ASC->InitAbilityActorInfo(this,this)
    → ASC->ServerSideInit()
       → InitializeBaseAttribute()            // NewObject + AddAttributeSetSubobject
       → InitializeAttributeFromDataTable()   // 按 Actor 类匹配 DT 行，SetXxx 写入 BaseValue
       → ApplyInitialEffects()                // 记录 ActiveEffectHandles
       → GiveInitialAbilities()               // InputTag 打到 Spec 动态 Tag 上
    → WeaponComponent->OnASCInitialized()
       → EquipWeaponGroup(DefaultWeaponGroupTag)
```

**客户端**

```
AController::AcknowledgePossession
 → ASC->InitAbilityActorInfo(Pawn, Pawn)
 → SpawnGameplayWidget()
```

**AI**

`AExtraAICharacter::ServerSideInit()` 自行初始化 GAS，不装备武器组。

---

## 2. 输入管线

### 2.1 完整链路

```
Enhanced Input Action
 → AExtraPlayerCharacter 的绑定函数（Move / Jump / OnNormalAttackStarted / ...）
 → UExtraAbilitySystemComponent::AbilityInputTagPressed(InputTag)
      ├─ OnAbilityInputTagPressed.Broadcast(InputTag)     // 已激活 GA 可接管
      ├─ 非权威端 → ServerNotifyInputTagPressed RPC      // 服务端也收到同一输入
      └─ 遍历 ActivatableAbilities，匹配 Spec 动态 Tag
           → PressedSpecHandles / HeldSpecHandles
 → APlayerController::PostProcessInput（每帧末）
 → ASC::ProcessAbilityInput()
      ├─ 先处理 Released：InputPressed=false + InputReleased 事件
      └─ 处理 Pressed：
           ├─ 已激活 → AbilitySpecInputPressed + InputPressed 复制事件（WaitInputPress 用）
           └─ 未激活 → TryActivateAbility（LocalPredicted 会连带服务端激活）
```

### 2.2 输入 Tag 绑定方式

- GA 在 CDO 上声明 `UExtraGameplayAbility::InputTag`。
- Give时 `UExtraAbilitySystemComponent::ApplyAbilityInputTag(Spec)` 把 Tag 写入 Spec 动态标签。
- 输入层只认 Tag，不认 GA 类；换武器组只换背后的 GA，输入映射不变。

### 2.3 特殊分派

| 场景        | 分派逻辑                                                             | 位置 |
|-----------|------------------------------------------------------------------|---|
| 空中攻击      | 一阶段发 `InputTag.AirDive`，二阶段发 `InputTag.LightAttack` 给 Phase2 空中连段 | `OnNormalAttackStarted` |
| 长按普攻      | 一阶段满足能量阈值后：`HeavyAttack`；二阶段 → `HeavyAttack` 或 满能量阈值后`AttackPro` | `OnReachHeavyThreshold` |
| 松开普攻      | 同时释放 Light/Heavy 两个 Tag                                          | `OnNormalAttackCompleted` |
| 长按技能      | 按住直到 Montage 的居合检测帧（AN标记）才衔接居合                                   | `AN_Skill02JuheCheck` |
| 跨 GA 接管输入 | `OnAbilityInputTagPressed` 广播（附 Server RPC）                      | `AbilityInputTagPressed` |

---

## 3. 武器管线

### 3.1 数据结构

`UExtraGameWeaponData`（DA）

```
WeaponGroups: Map<GroupTag, FExtraGameWeaponGroup>
DefaultWeaponGroupTag
```

`FExtraGameWeaponGroup`

```
WeaponEntries: TArray<FExtraGameWeaponEntry>
GrantedAbilities: TArray<TSubclassOf<UExtraGameplayAbility>>
GrantedEffects:   TArray<TSubclassOf<UGameplayEffect>>
AdditionalTags:   FGameplayTagContainer     // 形态 Tag（State.Phase1 / Phase2）
NumericParams:    Map<FName,float>
WeaponShouldNotCauseDamage: Set<FGameplayTag>  //不参与武器Trace的武器，例如弓、刀鞘等等，参与武器组只是为了显示
```

`FExtraGameWeaponEntry`

```
WeaponTag / DisplayName
WeaponMesh (SoftObject)
AttachSocketName（默认Socket） / AltAttachSockets(一次性使用Socket) / RelativeTransform
TraceSockets / TraceConfig (半径、最大子步距离、最大子步数)
```

### 3.2 装备 / 切换 / 卸载

**EquipWeaponGroup(GroupTag)**

```
1. 校验 DA 与 GroupTag
2. 同组重复装备 → ShowWeapon()
3. UnequipWeaponGroup()
     ├─ RemoveGrantedAbilities()
     ├─ RemoveGrantedEffects()
     ├─ RemoveLooseGameplayTags(旧组 AdditionalTags)
     ├─ HideGroupWeaponMeshes()
     ├─ 清空 HiddenWeaponEntries / 挂点覆盖
     └─ CurrentGroupTag = 空
4. CurrentGroupTag = GroupTag          // 复制触发+OnRep
5. UpdateGroupMeshPresentation()        // 各端生成/显示本组 Mesh
6. ApplyWeaponGroupEffects()
7. GrantWeaponGroupAbilities()
8. UpdateCharacterTags(Old, New)
9. OnWeaponGroupChanged.Broadcast
```

**关键点**

- GA/GE/Tag 的增删只在权威端执行；客户端只做 Mesh 表现。
- 切组不是销毁而是隐藏：Mesh 生成后保留，切组只改可见性，避免重复生成Mesh。
- `AdditionalTags` 是 LooseTag，不复制，客户端在 `OnRep_CurrentGroupTag` / `SyncGroupTagsToLocalASC` 中补齐，用来标记阶段1、2等等。
- 本地预测切组时服务端复制同值不会触发 OnRep，所以非权威端在 `UpdateCharacterTags` 里也会立即补 Tag。

### 3.3 Mesh 生命周期与显隐

```
SpawnWeaponMesh()
 ├─ 同步加载 SoftObject Mesh
 ├─ NewObject<UStaticMeshComponent> + RegisterComponent
 ├─ Attach 到角色 Mesh 的 AttachSocketName
 └─ 每个 Material Slot 创建 DMI，缓存到 WeaponDynamicMIs
```

显隐模型：

```
实际可见 = 属于当前组 && bWeaponVisible && 不在 HiddenWeaponEntries && Mesh 存在
```

- 整体显隐：`HideAllWeapon / ShowWeapon / ShowAllWeapons`，通过材质参数 `FadeAmount`（-1 不透明 → 1 全透明）做淡入淡出。
- `WeaponVisibilitySerial` 每次请求自增并复制，解决 bool 重复赋值不触发 OnRep 的问题。
- 逐武器显隐：`ShowWeaponEntry / HideWeaponEntry`。
- 临时挂点：`ShowWeaponEntryOnSocket` 只对当次展示有效，之后任何普通显示都回默认挂点。
- `AN_WeaponVisibility`：动画帧上操作单个武器或整组显隐，可指定临时 Socket。

### 3.4 武器轨迹扫描管线

入口：`ANS_WeaponTrace`（Montage 上标注命中区间）

```
NotifyBegin
 → WeaponComponent::BeginWeaponTrace(DamageWindowId)
    ├─ 仅权威端
    ├─ GatherTraceSocketsFromCurrentGroup()
    │    ├─ 跳过 WeaponShouldNotCauseDamage、手动隐藏、无 TraceSockets 的武器
    │    ├─ 组内第一把有 Socket 的武器的 TraceConfig 作为本窗口配置
    │    └─ 记录 Socket → Mesh 映射
    └─ 记录各 Socket 起始位置

NotifyTick（每个动画帧）
 → TickWeaponTrace()
    └─ 对每个 Socket：prev → cur 球体扫描
       → TraceSocketSegment()
          ├─ 帧间位移 / MaxStepDistance = 子步数（Clamp 到 MaxSubSteps）
          ├─ SweepMultiByChannel(WeaponTraceChannel, Sphere(TraceRadius))
          ├─ 忽略 Owner 与全部武器 Mesh
          ├─ 只结算带 ASC 的 Actor（地形/道具不产生伤害）
          ├─ 已命中黑名单去重（本窗口内）
          └─ SendHitEventForActor()：发 ability.damage 事件
                  TargetData = ActorArray + DamageWindowData(带有id，从GA实例中配置的Window找到对应的Data数据) 

NotifyEnd
 → EndWeaponTrace()：关闭窗口、清空 Socket/黑名单状态
```

**即时结算**：命中当帧即发事件，GA 收到后当帧应用 GE，保证手感。

**兜底**：GA `EndAbility` 会无条件调用一次 `EndWeaponTrace()`，防止蒙太奇异常终止导致窗口无法被清理。

**DS**：`AExtraCharacter` 构造函数把 Mesh 设为 `AlwaysTickPoseAndRefreshBones`，否则服务端 Socket 坐标冻结成纯平移，而Trace就是在服务端进行结算的。

---

## 4. 技能（GA）管线

### 4.1 类层级

```
UGameplayAbility
├─ UExtraAbilityBase（Abstract）
│   通用窗口/Push力施加机制
├─ UExtraPassiveGameplayAbility（Abstract）
│   └─ UGA_HitReact(受击)
│   └─ UGA_KnockDown(倒地，在受击一定次数后转换，或某些GA强制触发倒地)
├─ UExtraGameplayAbility
│   ├─ UGA_Combo
│   │   └─ UGA_ComboHeavy
│   │   └─ UGA_Combo_Phase_2
│   ├─ UGA_AirAttack
│   ├─ UGA_AirAttack_Phase2
│   ├─ UGA_HeavyAttack
│   ├─ UGA_HeavyAttackCharge
│   ├─ UGA_Evade
│   │   └─ UGA_Evade_Juhe
│   ├─ UGA_Skill_01
│   ├─ UGA_Skill_02
│   ├─ UGA_AttackPro_Phase_2
│   ├─ UGA_Burst01
```

### 4.2 基类提供的通用机制

| 机制              | 入口 | 生命周期                                                                         |
|-----------------|---|------------------------------------------------------------------------------|
| 后摇 CancelWindow | `UExtraAbilityBase::bEnableCancelWindow` | 进窗撤销自身 BlockTag + 登记到ASC的CancelGASpec上；任何 GACommit 即Cancel此GA；出窗恢复Block并撤销登记 |
| 霸体窗口            | `bEnableUninterruptible` | PreActivate 挂 LooseTag `State.Uninterruptible`；`AN_EndUninterruptible` 发事件移除 |
| 移动打断            | `bEnableMovementCancel` | 监听 `ability.cancel`，有移动就会发送此Tag，GA端收到即调用 `K2_CancelAbility`                  |
| 锁定追踪            | `bRotateToLockTarget` | PreActivate 注册 MW PreUpdate 回调，逐帧写 warp target，计算Rot,Loc，确定Montage的方向和落点     |
| 穿透追踪            | `bEnableForwardOvershoot` | 独立 warp target，区间开头算一次落点（能否到达目标身后），如果穿透，添加OverShootTag来激活依赖此Tag激活的镜头、Trace等  |
| 重力缩放            | `bEnableGravityScale` | PreActivate 缓存默认值并覆写，EndAbility 恢复                                           |
| 推力              | 始终监听 `ability.push.self` | AN_ApplyPush 带 FPushTargetData，GA 解析后 LaunchCharacter                        |
| 武器伤害            | `bEnableWeaponDamage` | 服务端监听 `ability.damage`，统一走 DoDamage，进入伤害管线，经MMC计算后返回结果                       |
| 范围伤害            | `bEnableAreaDamage` | 服务端监听 `ability.area.damage`，最终也会进入伤害管线                                       |
| 能量              | 攻击段配置 `EnergyGain` | 命中结算时按 Montage+Section 去重累积                                                  |

**EndAbility 的统一收尾**（顺序重要）：

```
IsEndAbilityValid 前置判断（防重复 end 的副作用，因为大部分逻辑在Super前要执行，而去重判断在Super中）
→ 必要时 HideAllWeapon（只在本 GA 仍是动画持有者时）
→ EndWeaponTrace 兜底
→ ClearAllRequests 相机兜底
→ 恢复为默认重力
→ 解绑 MW 回调、移除 warp target
→ 清空多段伤害调度
→ 清除穿透状态 Tag
→ Super::EndAbility
```

### 4.3 技能分段架构

标准形态：

```
前摇 Montage 段（可能带霸体）
   ↓ AN/ANS 事件
表现段（位移 / 伤害窗口 / 镜头 / 音效 / 武器显隐）
   ↓ AN_CancelWindow
后摇段（可被任意 GA / 移动输入打断）
```

- 分段由 Montage 的 Section + AN/ANS 表达，GA 不硬编码区间。
- 事件型 AN：`AN_SendGameplayEvent`（瞬时）与其特化子类，分发各种EventTag触发不同类型的回调。
- 区间型 ANS：`AN_GameplayEventWindow`、`ANS_WeaponTrace`、`ANS_CombatCamera`、`ANS_GravityScale`、`ANS_AudioEvent`。
- 特化子类只覆写 `GetEventTag / GetWindowBeginTag / GetWindowEndTag`，Tag 在面板隐藏，杜绝误配。

### 4.4 连段状态机（Combo）

```
ActivateAbility
 → PlayMontage(起始 Section)
 → 监听 ability.combo.change（Montage 上的 AN 发送）
 → 监听 WaitInputPress

进入下一段判定帧
 → AN 发 ability.combo.change.X
 → 取 Tag 最后一段作为 NextComboName（仅记录，不立即跳）

下一次攻击输入
 → TryCommitCombo()
    → Montage_SetNextSection + Montage_JumpToSection(NextComboName)
```

- `UGA_Combo`：纯手动节奏，连段区间由两个comboAN确定开头和结尾。
- `UGA_ComboHeavy`：覆写 `OnComboSectionChanged`，持续按住攻击键则自动续段；注册末段能量累计与重击切入帧（满足条件后直接进入重击，不再连段）。
- Skill01 强化combo：`bSkill01ComboBoost` → 下一次连段直跳末段并额外充能。
- **Section 不参与网络复制**：各端收到同一次输入后各自跳段；GA 不做跨端 Section 同步。

### 4.5 主要技能流程摘要

| 技能 | 核心流程                                                                                         |
|---|----------------------------------------------------------------------------------------------|
| `GA_ComboHeavy` | 按住自动连段 → 末段累计100能量 → 长按切入帧判定 → 能量满发重击输入激活，并结束自身                                              |
| `GA_Combo_Phase_2` | 复用父类连段；每次出手开 `State.JuheReady` 窗口（1.5s）                                                      |
| `GA_AirAttack` | 起手 → 循环下砸 → 落地段；落地检测 = Landed 委托 + 轮询双保险                                                     |
| `GA_AirAttack_Phase2` | 段1 → 段2 → 窗口内输入进入下一段，如果是第三段则下一帧交接给 `GA_AirAttack` ；段进度存 ASC Tag count,只要没有落地，切换GA后还能续段       |
| `GA_HeavyAttack` | 激活冻结全场敌方（CustomTimeDilation=0）→ 快照瞄准点 → 放箭帧 AN 逐支生成箭矢 → End 解冻                               |
| `GA_HeavyAttackCharge` | 起手段 → 未松手进循环段（Apply持续扣耐力 GE）→ 松手停当前段播结束段打出攻击                                                 |
| `GA_Evade` | 前冲/后闪按输入选择 → 可二次闪避 → `AN_EvadeToSprint` 转冲刺 → 空中需落地段 → 结束                                    |
| `GA_Evade_Juhe` | 复用闪避输入，无居合条件时进入空中/地面分支、有则进入居合架势、前冲段计数 `State.ProJuheCount，解锁强化重击`                            |
| `GA_Skill_01` | 单 Montage；结束时给普攻强化标记，能量+段数增幅                                                                 |
| `GA_Skill_02` | 两层充能（Cooldown GE stack）→ 地面升空斩 / 空中落地斩 → 长按到检测帧可衔接居合                                         |
| `GA_AttackPro_Phase_2` | `State.ProReady` 门控；激活即消费标记，整段二阶段只能触发一次                                                      |
| `GA_Burst01` | `State.Phase1` + `State.BurstReady` 门控；激活切 Phase2 武器组并换材质，涉及武器组切换不合适放在组中，所以放在角色InnativeGA中   |
| `GA_HitReact` | 被动；按攻击者方位选左右动画；时停期间等待解冻再播                                                                    |
| `GA_KnockDown` | 被动；接收 `ability.knockdown` + FPushTargetData(带有具体的PushVel和Override，通过SetByCallerTag传递参数)，执行击倒 |

### 4.6 CancelWindow 详细链路

```
AN_CancelWindow::NotifyBegin
 → ability.cancelwindow.begin
 → GA::EnterCancelWindow()
    ├─ SetShouldBlockOtherAbilities(false)     // 撤销自身对同类 GA 的封锁
    ├─ ASC->SetCancelWindowHolder(Handle)
    └─ AddLooseGameplayTag(State.CancelWindow)

任意 GA 的 CommitAbility
 → ASC->GetCancelWindowHolder()
 → 非自身则 CancelAbilityHandle(Holder)

AN_CancelWindow::NotifyTick
 → 检测到 MoveInput
 → 发 ability.cancel
 → 持有者 K2_CancelAbility

AN_CancelWindow::NotifyEnd
 → 发 ability.cancelwindow.end
 → GA::ExitCancelWindow()：恢复 Block + 解除登记 + 移除 Tag
```

---

## 5. 伤害管线（核心）

### 5.1 三条命中来源

```
A. 武器轨迹扫描（近战）
   ANS_WeaponTrace → WeaponComponent → ability.damage 事件

B. 范围检测（AOE）
   AN_AreaCheck → ability.area.damage 事件

C. 投射物（箭矢 / 剑气）
   AExtraProjectile::ProcessHit → 自行应用 GE
```

A、B 汇入 GA 基类的 `DoDamage / ApplyDamageProfile`；C 由投射物自行结算，但使用同一套伤害段结构。

### 5.2 攻击段配置

`FAttackSegmentConfig` 以 Montage Section 或 Montage 资产名为 key：

```
AttackSegmentConfigs: Map<FName, FAttackSegmentConfig>
  FAttackSegmentConfig
    ├─ DamageSegments: TArray<FDamageSegment>       // 默认段，没有多段伤害区间（即不为伤害区间配置ID）时使用
    ├─ DamageWindows:  TArray<FDamageWindowConfig>  // 按 WindowId 细分
    ├─ SegmentInterval                              // 多段间隔，即每个伤害触发时间
    ├─ EnergyGain                                   // 本段命中能量
    └─ bSendHitReact                                // 是否发受击表现

  FDamageSegment
    ├─ Coefficient        // 攻击力倍率
    ├─ Count              // 重复次数
    ├─ bKnockDown
    └─ KnockbackVelocity (攻击者本地空间 X前/Y右/Z上)
       + bOverrideXY / bOverrideZ
```

查找顺序：

```
GetEnergyGainMontage() / GetEnergyGainSection()
 → 有 Section：按 Section 精确匹配
 → 否则：按 Montage 资产名匹配
 → 都失败：本段无伤害
```

### 5.3 完整伤害链路

```
【服务端】
WeaponComponent::TraceSocketSegment
 → SendHitEventForActor(HitActor)
      GameplayEventData:
        EventTag = ability.damage
        TargetData = ActorArray(HitActor) + DamageWindowData(WindowId(没有配置的话会在结算端用默认伤害段))
        
GA（服务端）WaitGameplayEvent 回调
 → OnDamageEventReceived → DoDamage(Data)
    ├─ 非权威端直接返回
    ├─ GetAllActorsFromTargetData()
    ├─ FindAttackSegmentConfig()
    ├─ 解析 DamageWindowData.WindowId
    └─ ApplyDamageProfile(Targets, Config, WindowId)
         ├─ 选择 DamageWindows[WindowId] 或默认DamageSegments
         ├─ 根据配置的Count 展开成Count条 Segment遍历应用
         ├─ 能量：GetConfiguredEnergyGain() → ClaimEnergyValue() → AddEnergyValue()，一个段无论有多少Count只应用一次Energy
         └─ 每个目标创建一个 FDamageSequence：
              Target / Segments / Interval / bSendHitReact / bKnockDownUsed
              → ApplyNextDamageSegment(Target)

ApplyNextDamageSegment
 → 构造 GE Spec（GetDamageEffect()）
    SetByCaller:
      Data.Damage                = -AttackPower × Coefficient //应用值
      Data.HitReaction.Trigger   = 首段或击飞段 ? 1 : 0         //附带机制值
      Data.HitReaction.Level     = 击飞段 ? 1 : 0
      Data.HitReaction.KnockbackX/Y/Z
      Data.HitReaction.OverrideXY/Z
 → ApplyGameplayEffectSpecToTarget
 → 记录 Health + Shield 前后差值
 → SendDamageNumberCue(Target, HealthLoss + ShieldLoss)
 → 若还有下一段：按 SegmentInterval 定时递归
 → 全部完成：移除序列

【受击侧】
UExtraGameAttributeSet::PreAttributeChange
 → Health 下降且 Shield > 0 → 先扣 Shield BaseValue，剩余扣 Health

PostGameplayEffectExecute
 → Health 下降 → HandleHitReaction()
    ├─ Trigger <= 0 → 不表现（多段后续段）
    ├─ Level < 1 → SendHitReactEvent(Victim, Instigator)   //0 受击  1 倒地
    └─ Level >= 1 → 本地空间击飞向量转世界 → SendKnockDownEvent()
 → Clamp Health
 → AI 且 bRefillHealthOnZero → 回满Health

GA_HitReact / GA_KnockDown
 → 被动 GA 接收TriggerEvent → 播受击/倒地 Montage
```

### 5.4 范围伤害链路

```
AN_AreaCheck（Montage 伤害帧）
 → 发 ability.area.damage
    TargetData = FAreaCheckData(CenterMode / CenterOffset / Radius)
               + FDamageWindowData

GA 监听回调 OnAreaDamageEventReceived
 → PerformAreaDamage(CenterOffset, Radius, CenterMode, WindowId)
    ├─ 仅权威端
    ├─ Radius <= 0 → 用 GA.AreaDamageRadius
    ├─ CenterMode == Inherit → 按 GA.bAreaDamageUseLockTargetAsCenter 解析
    ├─ 圆心：
    │    Owner       = 角色位置 + XY 偏移（Z 沿用角色高度）
    │    LockTarget  = 锁定目标位置（无锁定回退角色）
    ├─ 遍历 AExtraCharacter：
    │    不同 Team + Health > 0 + 距离平方 <= Radius²
    ├─ Debug 绘制（可选）
    └─ 打包 ActorArray + WindowId → DoDamage()
```

### 5.5 投射物伤害链路

```
GA 生成投射物
 → InitProjectile(Source, DamageEffect, AbilityLevel, Dir, Speed, Life, RollOffset)
 → SetDamageSegments() / SetEnergyOnHit()

AExtraProjectile
 ├─ HitMode = BlockStop：命中即结算并销毁
 ├─ HitMode = PassThrough：命中 Pawn 只结算继续飞，撞静态几何才销毁
 ├─ IsDamageableEnemy：Team 判断
 ├─ ProcessHit：
 │    ├─ 同一目标只结算一次（HitActors）
 │    ├─ ApplyProjectileDamage：按 DamageSegments 构造 GE SetByCaller
 │    └─ ApplySourceEnergy：整支投射物只加一次能量
 └─ LifeTimer 超时销毁
```

### 5.6 能量管线

```
命中结算 ApplyDamageProfile
 → GetConfiguredEnergyGain()
    ├─ 读 AttackSegmentConfig.EnergyGain
    └─ 检查 EnergyGainRequiredTags（可选门控）
 → ClaimEnergyValue()：同一 Montage + Section 只返回一次
 → AddEnergyValue()：仅权威端，SetNumericAttributeBase
 → AttributeSet::PreAttributeBaseChange：Clamp [0, EnergyMaxValue]

用途：
 一阶段：能量满 → 解锁重击（GA_HeavyAttack）
 二阶段：居合前冲计数 → ProReady → AttackPro
```

### 5.7 伤害飘字

```
服务端 ApplyDamageSpecAndReportCue
 → 记录 Health + Shield 前后差值
 → UUExtraAbilitySystemStatic::SendDamageNumberCue(Target, Damage)
 → GameplayCueManager->InvokeGameplayCueExecuted_WithParams 广播,以CueTag为参数，权威端内部走Multicast
 → 各端本地 UGameplayCueNotify_DamageNumber::OnExecute
 → 生成 ADamageNumberActor（Screen 空间 UMG）
```

---

## 6. 动画管线

### 6.1 AnimInstance 数据流

`UExtraGameAnimInstance::NativeUpdateAnimation`

```
OwnerCharacter / OwnerMovementComp 缓存
 → 读取 CMC 状态：
     GroundSpeed（停步请求期间冻结缓存,由停步期间专用减速度进行减速）
     bIsFalling / bIsJumping / bIsWalking ，作为ABP状态机过渡标记
     bHasMoveInputCached（复制值，作为加速度依据）
 → 停步请求超时兜底，立刻进入停步
 → UpdateIdleActionSystem (待机动画更新)
```

暴露给 AnimBP 的线程安全函数：

- `HasAcceleration()`
- `bIsJumping() / bIsFalling() / bIsWalking()`
- `GetMoveMode()`（0 走 / 1 跑）
- `CalculateStrideBlend()` / `CalculateStandingPlayRate()`

### 6.2 步幅与播放速率

```
StrideBlend = Lerp(Walk 曲线(GroundSpeed), Run 曲线(GroundSpeed), W_Gait)
PlayRate    = (映射速度 / StrideBlend) / 缩放，Clamp 到 [0,3]
```

速度档映射统一使用 `UExtraGameMovementComponent::GetMappedSpeed()`（0 停 / 1 走 / 2 跑 / 3 冲刺），换速度值不用重画曲线。

### 6.3 停步 / 急停 / 转身管线

```
松开移动键
 → StopMoveInput()
    ├─ 记录 LastMoveInputDuration
    ├─ 清冲刺状态
    ├─ 若仍有战斗 Montage（非停步类）播放 → 只同步输入状态，不触发停步
    ├─ 轻触（< 0.2s）：
    │    ├─ |角差| < TurnSharpAngel → 急停 Montage（左/右）
    │    └─ 否则 → 原地转身 Montage（左/右）
    │    → 写入 MotionWarping target（TargetYaw）
    │    → Server_SetMoveInputState(ReleaseAction, TargetYaw)
    └─ 长按：
         → AnimInstance->RequestStop()
         → 等 FootPlant Notify 的落脚点
         → 起播左/右停步 Montage
         → Server_NotifyStopMontagePlayed(bLeft)
         → 模拟代理通过 RepPendingAction 同步播放
```

停步请求状态：

```
bRequestStop
 ├─ AnimInstance 每帧检查超时（StopRequestTimeout）
 ├─ CMC 停步窗口使用专用刹车减速度 StopBrakingDeceleration
 ├─ 新移动输入 / 跳跃 / 进入空中 → ClearStopRequest()
 └─ FootPlant → 播停步 Montage → ClearStopRequest()
```

### 6.4 Montage 播放与复制

- 播放：`UAbilityTask_PlayMontageAndWait`，或角色层的 `PlayAnimMontage`（停步/转身类）。
- 停止必须经 `UAbilitySystemComponent::CurrentMontageStop`（或 `StopMontageIfCurrent`），模拟端只认 ASC 的 `RepAnimMontageInfo`。
- 惯性化淡出策略停播：ASC 子类重写 `CurrentMontageStop`，补回被复制清零的 BlendOut 时长。
- Section 不复制：连段各端各自跳段。
- Slot 分层：`DefaultSlot` 用于停步/转身，`Ability` 用于 GA；跨 Slot 不互斥，可能叠加，为了处理这种情况，GA基类的PreActive中会检查当前是否有StopMontage相关在播并停止。

### 6.5 Idle 动作系统

```
Idle 状态计时 >= IdleChangeInterval
 → PickNextIdleAction()：O(1) 随机且避免连续重复
 → 设置 IdleActionIndex / CurrentIdleActionSequence
 → 持续 Duration 后回到 Idle
移动 / 空中 → 立即重置
```

---

## 7. 移动与旋转管线

### 7.1 速度曲线

```
GetMappedSpeed()
  0..1：0 → WalkSpeed
  1..2：WalkSpeed → RunSpeed
  2..3：RunSpeed → SprintSpeed

GetMaxAcceleration()          = MovementCurve[Mapped].X
GetMaxBrakingDeceleration()   = 停步窗口 ? StopBrakingDeceleration : MovementCurve[Mapped].Y
PhysWalking()                 = GroundFriction ← MovementCurve[Mapped].Z
```

### 7.2 旋转管线

```
PhysicsRotation()
 ├─ 服务端自主玩家且已有客户端权威 Yaw → 直接返回（客户端主导）
 ├─ 有 Montage 在播 → MW 接管朝向，使用RootComp的Yaw值作为朝向，本组件让位
 ├─ 速度 > 阈值且 Acceleration 非零 → 刷新 LastRotationTarget = Acceleration 方向
 └─ CustomPhysicsRotation()
      ├─ 计算相机 Yaw 变化率 CachedCameraYawRate
      ├─ 判定是否继续完成转向操作(有输入/速度没有衰减到停止旋转的Speed阈值)
      ├─ AirScale = IsFalling ? AirRotationScale : 1
      └─ SmoothCharacterRotation(LastRotationTarget, 输入层速率 × Air, 角色层速率 × Air)
           ├─ 第一层：TargetRotation 匀速逼近输入方向（RInterpConstantTo）
           └─ 第二层：角色指数逼近 TargetRotation（RInterpTo）

第二层速率：
  RotationRateCurve(MappedSpeed) × MapRange(CameraYawRate, 0..Max, 1..MaxMultiplier)
```

设计意图：

- 第一层消除输入跳变造成的硬拐点。
- 第二层仿照原生的指数收敛。
- 相机甩得越快，角色转得越快（跟手）。
- 空中按 `AirRotationScale` 线性削弱速率。

### 7.3 移动输入与 DS 同步

```
Move()
 ├─ 无输入 → 有输入跳变：ClearStopRequest / 取消停步 Montage / 记录移动起始时间
 │    → Server_SetMoveInputState(true, None, 0)
 ├─ 以 ControlRotation 的 Yaw 为基准前方向，将InputVal的XY映射到Yaw的XY方向上，最终合成一个Yaw坐标系上的方向向量
 ├─ 更新 TargetYaw（绝对方向，非角差）
 ├─ InputDirection = 单位输入方向
 ├─ Server_SendInputDirection(InputDirection)
 └─ AddMovementInput()

客户端每帧
 → Server_SyncClientYaw(角色 Yaw)（Unreliable）
 → 服务端立即 SetActorRotation + 标记客户端权威 Yaw

服务端复制给模拟代理（COND_SkipOwner）：
 bHasMoveInput / bWalkMode / RepPendingAction / RepTargetYaw / bIsJumping / InputDirection
```

### 7.4 冲刺 / 跳跃 / 空中状态

- 冲刺：`GA_Evade` 在 `AN_EvadeToSprint` 后截断蒙太奇，`bIsSprinting` 置位；Tick 中 `MaxWalkSpeed` 向目标速度插值。
- 跳跃：`JumpHeight` 在 BeginPlay 换算为 `JumpZVelocity = sqrt(2·g·h)`；跳跃会 `CancelAllAbilities` 并清停步请求。
- 空中：CMC 进入 Falling 时挂 `State.Airborne`；落地清空：
  - 空中闪避预算 `AirEvadeCharges`
  - 空中普攻段数 `State.AirAttackStage`
  - 二阶段离开时清 `ProJuheCount / ProReady`
- 空中闪避：每次浮空初始 1 次，上限 2 次；空中攻击每浮空恢复 1 次。

### 7.5 模拟代理表现同步

```
Tick（SimulatedProxy）
 ├─ TargetYaw = RepTargetYaw
 ├─ RepPendingAction 变化且未消费 → 播急停/转身/停步 Montage
 └─ RepRootMotion 由播放中变停止 → CancelStopMontageIfPlaying()
```

---

## 8. 锁定与 MotionWarping 管线

### 8.1 锁定检测

```
ULockOnComponent::BeginPlay（仅服务端）
 → 周期 Timer（DetectInterval）

UpdateLockTarget()
 ├─ bHoldTargetUntilBreak 且当前目标仍有效且在 LockBreakRange 内 → 保持
 ├─ 否则 OverlapMultiByChannel(Sphere(LockRadius), ECC_Pawn)
 ├─ IsValidTarget：
 │    不同 Team / Health > 0 / 可选 LOS
 ├─ 取最近目标
 └─ CurrentLockTarget 复制给各端
```

### 8.2 GA 的 warp target 三态

`UpdateLockOnWarpTarget()` 由 MW 的 `OnPreUpdate` 每帧调用：

| 状态 | 位移 | 旋转 | 落点                                                                                |
|---|---|---|-----------------------------------------------------------------------------------|
| 有锁定目标 | 是 | 是 | 目标位置沿「目标→自身」退 `LockOnWarpStandoff`（为了避免胶囊体重合产生的挤压）；超过 `MotionWarpMaxMoveDist` 则钳制 |
| 无目标、有输入 | 否 | 是 | 朝输入方向                                                                             |
| 无目标、无输入 | 否 | 否 | 不干涉                                                                               |

补充规则：

- 只水平化（Z 取自身高度）。
- 服务端远端玩家实例只 warp 位移、不 warp 旋转（旋转由客户端 Yaw 抄写主导）。
- modifier 的 `bWarpTranslation / bWarpRotation` 每帧同步，但只做减法：以 NMS 面板勾选为基线，GA 只能关闭、不能主动开启。
- 因此后摇段可以在 NMS 里关掉 Warp Translation，实现「只转向不追击」。

### 8.3 穿透目标（Forward Overshoot）

```
ForwardOvershootTargetName（独立 warp target）
 → 区间开头计算一次：
    DashDir = 目标 - 自身（水平化）
    落点   = 目标 + DashDir × OvershootDistance（Z 取自身高度）
    超过 MaxOvershootWarpDist 则钳制
    bPassThroughTarget = 落点是否越过目标
 → 缓存方向与落点，区间内固定
 → 若会穿身，挂 State.ForwardOvershoot（供相机条件触发）
 → 区间结束或 GA 结束清除
```

### 8.4 停步 / 转身的 MW

- 急停 warp target：`QuickStopTarget`
- 转身 warp target：`TurnTarget`
- 目标旋转统一使用松手时缓存的绝对 `TargetYaw`，而不是「当前朝向 + 角差」，避免按键期间已转角度被重复计入。

---

## 9. 相机管线

### 9.1 完整链路

```
Montage 时间线
 → ANS_CombatCamera::NotifyBegin
    ├─ 检查 RequiredOwnerTag（不满足则挂 Pending，区间内每帧复核）
    ├─ CombatCameraComponent->PushRequest(FCombatCameraRequest) → 返回 RequestId
    └─ 按 Mesh 记录 RequestId（NotifyState 是共享实例）
 → ANS_CombatCamera::NotifyEnd → PopRequest(RequestId)

UCombatCameraComponent::TickComponent（每帧）
 ├─ FindActiveRequest()：Priority 最高；相同优先级取最新 Push
 ├─ 计算目标值：
 │    LocY / LocZ / ArmRotation / ArmLength / FOV
 │    未修改项保持当前值
 ├─ BlendTime = 请求 BlendInTime
 │    镜头切换时取 max(新 BlendIn, 旧 BlendOut)
 ├─ VInterpTo / RInterpTo / FInterpTo 趋近
 ├─ 写入 FollowCamera（位置、FOV）
 ├─ UpdateBoomRotation()：臂朝向接管
 └─ 输出 TargetArmLength（有请求或未回到基准时接管，否则交还 Zoom）
```

### 9.2 请求语义

`FCombatCameraRequest` 是「意图」，不是「最终值」：

- `bModifyXxx` 控制是否覆盖对应通道。
- 所有请求出栈后，参数统一淡出回基准值。
- 被更高优先级请求顶掉的镜头，按自己的 `BlendOutTime` 退场。
- `ClearAllRequests()` 使用 `InterruptedBlendOutTime`（GA 提前结束时，Montage中的ANS没有走到End进行相机清理时改出的Time）。

### 9.3 臂朝向接管

触发条件：

- `bUseCharacterFacingBasis`（对齐角色朝向 / 前置 180°）
- `bLockLookInput`
- 臂旋转偏移未归零（必须握着写入权，否则被 `bUsePawnControlRotation` 顶掉）

三不变量：

1. 接管期间不读 SpringArm 组件旋转（相对父级会漂移），只认本组件上次写出的 `LastBoomWorldRotation`。
2. 接管开始 / 交还时把「基准朝向」折进 `ControlRotation`，偏移不能重复叠加。
3. 偏移未淡出完不交还写入权。

### 9.4 相机震动

- `AN_CameraShake`：单点震动；`bForSelf` 只震本地玩家，否则世界范围按距离衰减。
- `UExtraCurveCameraShake` + `UExtraCurveCameraShakePattern`：曲线驱动震动资产。
- 服务端不播避免客户端重复震动。

---

## 10. 音频管线

```
ANS_AudioEvent::NotifyBegin
 ├─ 专用服务器跳过
 ├─ 清理失效条目；同 Mesh 上一次声音先停（防叠加）
 ├─ ResolveProbability(TagProbabilityInfo, Owner)
 │    → 取 Owner 拥有的第一个匹配TagProbabilityInfoz中含有 Tag 的对应的 Probability / Cooldown
 ├─ 概率判定失败（FRand值和Pro比较） → 不播
 ├─ 冷却判定（UAudioEventCooldownSubsystem : UGameInstanceSubsystem）
 │    Key = Owner + AN/ANS 实例 + Tag
 │    → 未结束冷却 → 不播
 ├─ PlaySound：
 │    bFollow → SpawnSoundAttached（Socket）
 │    否则     → SpawnSoundAtLocation
 │    FadeIn(FadeDuration, FadeCurve)
 ├─ 记录 ActiveAudioComponents[Mesh] / StartedMeshComps
 └─ 冷却时间 > 0 → MarkPlayed()

NotifyEnd
 ├─ 未真正播放（概率/冷却跳过）→ 不播尾音
 ├─ TrailingAudioEvent → 播放尾音
 └─ bKeepAlive ? 保留自然播完 : FadeOut
```

**设计要点**

- `UAnimNotifyState` 是资产级共享实例，必须按 `USkeletalMeshComponent` 分桶，否则多角色共用动画时互相覆盖。
- 冷却记录放 `UGameInstanceSubsystem`：跨 Level 保留，多端各自独立。
- 冷却 Key 用 `FObjectKey`，避免裸指针重定向导致键冲突。

---

## 11. UI 管线

### 11.1 主 HUD

```
AExtraPlayerController::AcknowledgePossession
 → SpawnGameplayWidget()（仅本地 PC）

UGameplayWidget::NativeConstruct
 ├─ 获取 OwningPawn 的 ASC
 ├─ HealthBar：绑定 Health / MaxHealth + Shield 覆盖条
 ├─ StaminaBar：绑定 Stamina / MaxStamina
 ├─ ComboGauge：绑定 EnergyValue
 └─ ComboValueText：EnergyValue / EnergyMaxValue 任一变化即刷新
```

所有条均由属性变化委托驱动，不轮询。

### 11.2 头顶血条

```
AExtraCharacter::BeginPlay
 → 非 DS、非本地玩家 → ConfigureOverHeadStatusWidget()
 → UOverHeadStatsGauge::ConfigureWithASC()
 → 两个 Timer：
     UpdateHeadGaugeVisibility()：与本地 Pawn 距离平方 > 阈值 → 隐藏
     UpdateHeadGaugeRotation()：LookAt 相机，Roll 归零
```


### 11.3 伤害飘字

```
服务端 → GameplayCueManager → 各端 OnExecute
 → ADamageNumberActor
    ├─ 目标骨骼（默认 head）+ SpawnHeightOffset
    └─ Screen 空间 UMG 飘字
```

---

## 12. 数据驱动一览

| 数据 | 资产类型 | 位置 |
|---|---|---|
| 武器组（Mesh/挂点/扫描/GA/GE/Tag/参数） | DataAsset | `WeaponSystem/ExtraGameWeaponData` |
| 角色初始属性 | DataTable | ASC `AttributeDataTable` |
| 移动加速度/减速度/摩擦 | CurveVector | CMC `MovementCurve` |
| 转向速率 | CurveFloat | CMC `RotationRateCurve` |
| 步幅混合 | CurveFloat | AnimInstance `StrideBlend_N_Walk/Run` |
| 攻击段伤害/能量/窗口 | GA Class Defaults 结构体 | `FAttackSegmentConfig` |
| 伤害 GE | GameplayEffect 资产 | GA `DefaultWeaponDamageEffect` / 投射物注入 |
| 镜头 | ANS 上的 `FCombatCameraRequest` | Montage |
| 音频概率/冷却 | ANS 上的 `FAudioEventProbabilityCooldownInfo` | Montage |
| 相机震动 | Curve CameraShake 资产 | `AN_CameraShake` |
| 时停/能量/连段等数值 | GA/角色/属性集配置 | 各自 Class Defaults |

---

## 13. DS 网络同步总表

### 13.1 复制属性

| 类 | 属性 | 条件 | 用途 |
|---|---|---|---|
| `AExtraCharacter` | `TeamID` | 默认 | 敌我判定 |
| `AExtraPlayerCharacter` | `bHasMoveInput` | `COND_SkipOwner` | 模拟代理动画/服务端加速度判定 |
| | `bWalkMode` | `COND_SkipOwner` | 动画档位 |
| | `RepPendingAction` | `COND_SkipOwner` | 急停/转身/停步动作 |
| | `RepTargetYaw` | `COND_SkipOwner` | 模拟代理 MW 目标 |
| | `bIsJumping` | `COND_SkipOwner` | 区分起跳/掉落 |
| | `InputDirection` | `COND_SkipOwner` | 模拟端表现 |
| `UExtraGameWeaponComponent` | `CurrentGroupTag` | 默认 + OnRep | 各端生成武器组 |
| | `bWeaponVisible` | 默认 | 整体显隐 |
| | `WeaponVisibilitySerial` | 默认 + OnRep | 每次显隐请求必然触发 |
| `ULockOnComponent` | `CurrentLockTarget` | 默认 + OnRep | 锁定目标 |
| `UExtraGameAttributeSet` | Health/MaxHealth/AttackPower/Stamina/MaxStamina/Shield/EnergyValue/EnergyMaxValue | `REPNOTIFY_Always` | 属性表现 |
| `AExtraArrow` | `ArrowMesh` | OnRep | 箭身 Mesh 各端一致 |

### 13.2 RPC

| RPC | 方向 | 类型 | 用途 |
|---|---|---|---|
| `Server_SetMoveInputState` | C→S | Reliable | 移动输入状态 + 松手意图 + TargetYaw |
| `Server_SyncClientYaw` | C→S | Unreliable | 客户端权威朝向 |
| `Server_SendInputDirection` | C→S | Reliable | 输入方向 |
| `Server_ChangeWalkMode` | C→S | Reliable | 走跑切换 |
| `Server_NotifyStopMontagePlayed` | C→S | Reliable | 停步 Montage 起播同步 |
| `Server_SetHoldingAttack` | C→S | Reliable | 按住攻击状态 |
| `ServerNotifyInputTagPressed` | C→S | Reliable | 输入 Tag 上行 |
| `Server_OpenJuheReadyWindow` | C→S | Reliable | 居合窗口先行同步 |
| `MulticastLaunchCharacter` | S→All | Reliable | 击飞各端本地执行 |

### 13.3 约束

1. **Section 不复制**：连段必须各端凭同一输入各自跳段。
2. **Montage 停止只走 ASC**：直连 AnimInstance 会导致模拟端不同步、补帧被跳过。
3. **惯性化停播 BlendTime 会丢**：`CurrentMontageStop`已补回判断。
4. **LooseTag 不复制**：形态 Tag、CancelWindow、JuheReady 等需各端自行补齐或由 RPC 驱动。
5. **服务端骨骼姿势需刷新**：`AlwaysTickPoseAndRefreshBones = true`。
6. **模拟代理无 Acceleration**：AnimInstance 强制置 0，表现依赖复制状态。
7. **击飞不能只在服务端推**：客户端 ServerMove 会覆盖，必须 Multicast。

---

## 14. 扩展检查清单

### 新增一个攻击技能

1. 继承 `UExtraGameplayAbility`（连段类继承 `UGA_Combo`）。
2. 配置 Montage，并放置所需 AN/ANS：
   - 伤害：`ANS_WeaponTrace` 或 `AN_AreaCheck`
   - 镜头：`ANS_CombatCamera`
   - 音效：`ANS_AudioEvent`
   - 后摇：`AN_CancelWindow`
   - 位移：MotionWarping NMS + `bRotateToLockTarget`
3. 在 GA Class Defaults 配置 `AttackSegmentConfigs`（Section → 伤害段/能量/表现）。
4. 设置 `InputTag`，加入武器组 `GrantedAbilities`。
5. 确认 `bEnableWeaponDamage` / `bEnableAreaDamage` / `bEnableCancelWindow` 等开关。

### 新增武器组 / 形态

1. 在 `UExtraGameWeaponData` 添加 GroupTag 与 `FExtraGameWeaponGroup`。
2. 配置 WeaponEntries（Mesh、挂点、TraceSockets）。
3. 配置 GrantedAbilities / GrantedEffects / AdditionalTags（形态 Tag）。
4. 若为新形态，确保输入分派与 GA 门控读取该 Tag。

### 新增相机镜头

1. 在 Montage 上放 `ANS_CombatCamera`。
2. 填 `FCombatCameraRequest`（臂长/旋转/偏移/FOV/朝向基准/锁 look/优先级）。
3. 需要按运行时条件触发时填 `RequiredOwnerTag`。
4. 用 `CombatCamera.Debug.Override` 实时调参后再抄回。

### 新增音效

1. 在 Montage 放 `ANS_AudioEvent`。
2. 配主音效、Socket、跟随、淡入淡出、尾音、KeepAlive。
3. 配概率/冷却 Tag 列表（按 Owner 状态优先级）。
4. 多角色共用动画无需额外处理（已按 Mesh 分桶）。
