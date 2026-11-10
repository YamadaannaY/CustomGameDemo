# 开发 Bug 清单

统计范围：2026-08-28 ~ 2026-10-21，覆盖 46 个 Bug/异常类会话。
本版为整理后的记录：33 条已定位问题（19 条已解决 / 14 条推定解决），另列 5 条未闭环、3 条非 Bug。

---

## A. 网络同步与 GAS 执行

### DS 模式下普攻武器未显示 · 2026-10-04 · 已解决
- **现象**：DS 客户端普攻无武器，日志 current group 为 'None'，模拟端也不可见。
- **根因**：CurrentGroupTag 不是复制属性；客户端不走装备流程，武器 Mesh 只在服务端生成。
- **修法**：组件开启复制、Tag 改 `ReplicatedUsing`；抽出表现函数，服务端直调、客户端 OnRep 走同一路径。
- **文件**：ExtraGameWeaponComponent.h/.cpp

### DS 模式下 ComboGA 网络同步 · 2026-10-06 · 已解决
- **现象**：DS 下连招只能触发第一段；模拟端无法区分跳跃/下落，落地动画被跳过。
- **根因**：GameplayEvent 纯本地派发，服务端收不到二段输入 → 停在一段 EndAbility 并把客户端二段带停；Montage Section 不复制；JumpCurrentCount 未复制。
- **修法**：改 Lyra 式 InputTag 绑定 + AbilityInputTagPressed/Released，ProcessAbilityInput 补 InvokeReplicatedEvent；两端各跳各段；加 bIsJumping 复制镜像。
- **文件**：ExtraAbilitySystemComponent、GA_Combo、ExtraPlayerCharacter、ExtraGameAnimInstance

### GA 基类 MW 区间触发服务端修正 · 2026-10-06 · 已解决
- **现象**：DS 下 GA 基类两个 MW 区间触发时服务端做位置修正，角色被拽回。
- **根因**：写 warp target 前有 `!Pawn->IsLocallyControlled()` 早退，DS 下恒 false → 服务端从不写 target → 未 warp 的根位移与客户端 ServerMove 差数百 cm，触发 ClientAdjustPosition。
- **修法**：去掉早退让服务端写 target；锁定目标经 Server_SetLockTarget 上报，服务端 bWarpRotation=false。
- **文件**：ExtraGameplayAbility、ULockOnComponent、ANS_IgnoreCharacterCollision

### DS 模式下武器 Trace 无伤害 · 2026-10-07 · 已解决
- **现象**：DS 下武器轨迹不造成伤害；服务端 socket 只随根位移平移，空中挥砍时轨迹贴地。
- **根因**：ACharacter 默认 `AlwaysTickPose`，服务端只 tick pose 不 RefreshBoneTransforms → AN 照常触发但骨骼/socket 姿势冻结，扫的是冻住的刀。
- **修法**：构造改为 `AlwaysTickPoseAndRefreshBones`。
- **文件**：ExtraCharacter、ExtraGameWeaponComponent、ANS_WeaponTrace

### DS 模式下 Montage 打断后移动状态异常 · 2026-10-07 · 已解决
- **现象**：DS 下 Montage 被移动打断后可以移动，但以 Idle 姿势平移；单机正常。
- **根因**：`CancelStopMontageIfPlaying` 依赖 `GetCurrentActiveMontage`（只返回最后播放者），而 Ability 和停步用的是两个不同 Slot，两个 Montage 一起播、不会相互打断，比较全部落空 → 停步蒙太奇未停 → OnMontageEnded 不触发 → bRequestStop 残留 → GroundSpeed 冻结。
- **修法**：遍历 6 个停步蒙太奇做 `Montage_IsPlaying + StopWithBlendOut` + ClearStopRequest，在 GA 的 PreActivate 中调用。
- **文件**：ExtraPlayerCharacter、ExtraGameAnimInstance、ExtraGameplayAbility

### 能量充足时居合客户端不一致 · 2026-10-10 · 已解决
- **现象**：1. 能量足够时本地端不进居合、能量 UI 不涨，模拟端却进入；2. 居合架势下普攻无前冲。
- **根因**：1. EnergyValue 未配置复制，两端门控数据不一致；2. `WaitInputPress` 只收本 GA 自身的 InputTag(Dodge)，收不到其他 GA 的输入，而居合要等左键/右键分别进入前冲和后撤。
- **修法**：GA 输入改为 ASC 广播 `OnAbilityInputTagPressed` 并 RPC 同步，按 Tag 分流。
- **文件**：ExtraAttributeSet、ExtraAbilitySystemComponent、GA_Evade_Juhe

### DS 下自动连段窗口延迟与抖动 + 重击客户端没有生成箭矢 Mesh · 2026-10-11 · 已解决
- **现象**：1. DS 按住普攻自动连段不按窗口立即进段且每段跳变；2. 重击射箭客户端看不到箭。
- **根因**：1. bHoldingAttack 不复制，服务端只等客户端合成输入 RPC，与本端段变更 AN 竞态 → 晚一个窗口且消费上一窗口的陈旧目标；跳变是自动续段误消费「回第一段」的 change tag，把段位置拉回 0；2. 箭矢网格在权威分支注入、非复制属性。
- **修法**：`Server_SetHoldingAttack` 上行按住态，两端各自在段变更帧续段；自动续段跳过「目标段 == 当前段」；箭矢网格改 ReplicatedUsing + OnRep。
- **文件**：GA_Combo / GA_ComboHeavy、ExtraPlayerCharacter、ExtraArrow

### 模拟端 Evade 惯性化淡出异常 · 2026-10-13 · 已解决
- **现象**：模拟端取消 Evade 时惯性化淡出变成跳变；空中 Evade 模拟端抖动、落地瞬间朝向突变。
- **根因**：惯性化停播时引擎把 Montage 实例 BlendTime 归零，而 GAS 复制的恰是这个 float，模拟端 `CurrentMontageStop(0)` → 0 时长过渡；落地朝向是 PhysicsRotation 期间 LastRotationTarget 冻结在旧输入方向。
- **修法**：覆写 `CurrentMontageStop` 把 0 时长还原为资产 BlendOut；PhysicsRotation 同步 LastRotationTarget。warp target 复制方案实测无效已回退。
- **文件**：ExtraAbilitySystemComponent、ExtraGameMovementComponent

### Skill02 落地 Montage 网络同步 · 2026-10-18 · 已解决
- **现象**：模拟端空中落地不播 GA 配置的 Land 三段，改由状态机播 Falling/落地恢复动画，但根位移正常。
- **根因**：Loop/Land 段播停直连 `AnimInstance->Montage_Play/Stop`，不写 ASC 的 RepAnimMontageInfo；模拟端只消费该复制通道。
- **修法**：播放改 `ASC->PlayMontage`、停止改 `StopMontageIfCurrent`，段自循环保留。
- **文件**：GA_Skill_02.cpp

### Cancel-frame GA 打断 / 霸体失效 · 2026-08-28 · 已解决
- **现象**：技能 GA 激活时加 `State.Uninterruptible`，其他 GA 仍能打断。
- **根因**：UE5.6 的 `ActivationBlockedTags` 走 `DoesAbilitySatisfyTagRequirements` → `CheckForBlocked(GetOwnedGameplayTags())`，读的是 ASC 的 GameplayTagCountContainer；而 `Spec->DynamicAbilityTags` 只作 GE source tags，不参与 block 判定，tag 加了等于没加。
- **修法**：改用 `AddLooseGameplayTag` / `RemoveLooseGameplayTag`，用 bUninterruptibleActive 守卫避免重复 Remove 成负值；AN 发事件释放，EndAbility 兜底。
- **文件**：UExtraAbilitySystemStatic、ExtraGameplayAbility、AN_EndUninterruptible、GA_Skill_01/Combo/HeavyAttack/Evade

### Weapon-switch GA 自我移除 · 2026-09-04 · 已解决
- **现象**：变身大招 GA_Burst01 激活后要切换武器组，但卸组会把它自己也清掉，动画中断。
- **根因**：GA 的 spec 挂在第一阶段组的 ActiveAbilityHandles 中，`UnequipWeaponGroup → RemoveGrantedAbilities` 对该表整体 ClearAbility，不区分正在运行的大招实例。
- **修法**：把 GA 移入 ASC 的 InnateAbilities 常驻，用 State.Phase1 门控激活；ActivateAbility 开头调 SwitchWeaponGroup 切到 Phase2。
- **文件**：ExtraGameWeaponComponent.cpp、GA_Burst01.h/.cpp、UExtraAbilitySystemStatic.h/.cpp

### 空中居合退出后普攻仍进前冲 · 2026-09-25 · 推定解决
- **现象**：空中居合中再次后撤退出居合后，按普攻仍进空中居合前冲；地面正常走普攻。
- **根因**：退出居合只调 `RemoveJuheState()` 放行普攻 GA，GA 自身 `OnJuheAttackInput` 的闸门 `bJuhePhaseEnded` 未关；后撤段异步播完才 EndAbility，期间普攻仍被本 GA 接住并 `StartJuheForward()`。空中架势段分界事件晚到故必现。
- **修法**：`OnJuheDodgeInput` 退出时置 `bJuhePhaseEnded=true` 并清前冲窗口/接续 Timer；`EnterLandPhase` 同样置位，落地段按后摇处理。
- **文件**：GAS/GA_Evade_Juhe.cpp

### Evade GA 取消导致卡顿、此后无法触发 · 2026-10-11 · 推定解决
- **现象**：一阶段常规 Evade 仅最初可用，随后卡顿并持续弹「GA 被取消」，之后无法触发。
- **根因**：`GA_Evade::EndAbility` 冷却分支内 `if(!K2_HasAuthority()) return;` 在 DS 客户端跳过定时器清理、LandedDelegate 解绑、停 Montage 与 `Super::EndAbility()`，spec 永停 Active；残留 WaitGameplayEvent 被 AN_CancelWindow 每帧事件反复接住，EvadeFacingTimer 每帧写 MW。
- **修法**：把 `K2_HasAuthority()` 并入条件，仅跳过施加连闪冷却 GE，不早退收尾。
- **文件**：GAS/GA_Evade.cpp

### 丢失锁定后 MotionWarping 仍朝旧目标强转 · 2026-09-06 · 推定解决
- **现象**：走出锁定范围丢目标后，MW 仍生效并朝旧目标强转。（同会话首问「多区间能否共用同一 warp target」结论是可行，需同名且区间不重叠。）
- **根因**：`UpdateLockOnWarpTarget` 在 `GetLockTarget()` 为空时直接 return，未移除 MW 组件内已注册的旧 target；MR 区间按 name 仍查到缓存朝向。`EndAbility` 也只清定时器。
- **修法**：无目标时改为 `RemoveWarpTarget(LockOnWarpTargetName)`，重新锁定后下一 tick 自动恢复；EndAbility 兜底再移除一次。
- **文件**：GAS/ExtraGameplayAbility.cpp

### Montage_Stop(-1) 不走资产 BlendOut · 2026-09-11 · 推定解决
- **现象**：EndGA 中 `Montage_Stop(-1, ActiveMontage)` 本意用资产 BlendOut，实际瞬间切出。
- **根因**：`UAnimInstance::Montage_Stop` 没有「负数 = 取资产配置」的约定，-1 被当 BlendOutTime，≤0 走无混合立即终止；该语义只在上层 `CurrentMontageStop` / `PlayMontageAndWait` 中实现。
- **修法**：改用 `Montage_StopWithBlendOut(ActiveMontage->BlendOut, ActiveMontage)` 保留 BlendOption/BlendProfile；或传 `GetBlendTime()`。
- **文件**：GAS/ExtraGameplayAbility.cpp

### 动画编辑器预览 Actor 无 ASC 报错 · 2026-09-14 · 推定解决
- **现象**：预览动画时 CancelWindow 的 ANS 发 ability.cancelwindow.begin/end 报 Invalid ability system。
- **根因**：`AN_CancelWindow::SendCancelWindowEvent` 未判空 ASC 就 `SendGameplayEventToActor`；预览 Actor 没有 ASC。基类 `AN_SendGameplayEvent` 有判空故不报。
- **修法**：`SendCancelWindowEvent` 增加 ASC 判空，无 ASC 直接返回。
- **文件**：AN/AN_CancelWindow.cpp

---

## B. 动画 / 移动 / 碰撞

### 地面 EvadeGA 跳跃距离异常 · 2026-09-19 · 已解决
- **现象**：地面 Evade 进入时按跳跃会跳出很远。
- **根因**：前冲位移由 root motion 驱动，CMC 折算出的水平 Velocity 峰值上千；`Jump()` 只写 Z 速度、不清 XY，空中又无水平阻尼，残速整段滞空生效。已通过移除 MW 排除 MW 嫌疑。
- **修法**：`Jump()` 在 CancelAllAbilities 后把水平速度按 MaxWalkSpeed 截断（保留方向），改用 `Movement->Velocity` 访问。
- **文件**：ExtraPlayerCharacter.cpp、GA_Evade.cpp

### ComboGA 二阶段圆周移动碰撞抖动 · 2026-09-20 · 已解决
- **现象**：二阶段 Combo 的圆周轨迹经过目标中心时抖动；即使开启 IgnoreCollision 仍抖。
- **根因**：`MoveIgnoreActorAdd` 只影响 sweep 和移动解算，胶囊重叠仍必然发生；CMC 的 `bEnablePhysicsInteraction` 斥力按 overlap 推开双方，且忽略只做了单向，与 root motion 每帧互顶。
- **修法**：ANS 区间内对双方 `MoveIgnoreActorAdd`，临时关闭双方 bEnablePhysicsInteraction，BlendOut 时按原值恢复（该字段是 uint8 位域，备份需显式转 bool）。
- **文件**：ANS_IgnoreCharacterCollision.h/.cpp

### 跳斩 PushAN 导致角色滑到目标身后 · 2026-09-20 · 已解决
- **现象**：跳斩下砸时角色被水平拖到目标头顶、踩胶囊滑至身后；紧贴时相机臂被压短导致镜头跳到身前。
- **根因**：warp 落点 = 目标位置 − 朝向×20cm，落在两胶囊半径和（约 80cm）之内；warp 每帧抄自身 Z，Z 轴不 warp，垂直位置只由推力与重力决定；落到胶囊上半球面（在可行走角内被 CMC 判为 floor）后沿法线滑开；SpringArm 探测不走 MoveIgnoreActors，且 ECC_SpringArm 与武器扫描共用 GameTraceChannel1。
- **修法**：新增 `LockOnWarpStandoff` 可配退让距（默认 20，GA_AirAttack 覆写 100）；ECC_SpringArm 改到 GameTraceChannel3，角色胶囊对该通道设 ECR_Ignore。
- **文件**：ExtraGameplayAbility.h/.cpp、GA_AirAttack.cpp、ExtraCharacter.cpp

### AirControl 空中旋转失效 · 2026-09-28 · 已解决
- **现象**：空中无法转向；修正后 AirRotationScale=1 仍抑制过大。
- **根因**：`MoveUpdatedComponent` 第三参是绝对旋转，再乘 0.1/AirRotationScale 等于拽向世界 0°；空中目标取速度方向，而 AirControl 只有 0.05。
- **修法**：削弱改作用在插值速率；空中改用最后输入方向，地面/空中统一目标源。
- **文件**：ExtraGameMovementComponent.h/.cpp

### 胶囊体重叠碰撞失效 · 2026-10-10 · 已解决
- **现象**：DS 下客户端角色胶囊可互相重叠，仅剩斥力顶开，忽略碰撞永久残留。
- **根因**：ANS 是蒙太奇资产的共享 Instanced 子对象，OwnerCharacter/IgnoredActors 只有单槽位；NotifyBegin 先换 owner 再 ClearIgnore，用新世界的 A 删旧世界的 B 恒为空操作，随后无条件 Reset 丢失记录 → 最先播的服务端那对永久残留。收尾又只认 NotifyEnd，惯性化提前停播时该信号缺席。
- **修法**：状态改按角色分桶 Map，每帧按「自己是否有窗口」重算期望忽略集合并差量对齐；自续期 Timer 以「蒙太奇是否还在播」兜底；恢复双向忽略。
- **文件**：ANS_IgnoreCharacterCollision、ExtraCharacter（部分回滚）

### 受击动画与碰撞点判定 · 2026-10-18 · 已解决
- **现象**：已造成伤害且受击事件已注册，但受击 Montage 不播；后续 KnockDown 在模拟端比本地先落地、网格穿地。
- **根因**：受击判定放在 `SetHealth` 之后，而 SetHealth 再次触发 PreAttributeChange 冲掉记录的前值，新旧值恒相等；KnockDown 三段 Montage 勾了 Root Motion，模拟端在复制位置上又叠一份根位移。
- **修法**：判据改用 GE 的 `Magnitude < 0` 并移到 SetHealth 前；击飞改由 `AN_ApplyPush` 施力并取消 Root Motion。另修重触发：`bRetriggerInstancedAbility` + 先解绑回调再 EndTask。
- **文件**：ExtraGameAttributeSet、GA_HitReact、GA_KnockDown、AN_ApplyPush

### 连段攻击时角色左右小幅位移 · 2026-09-20 · 推定解决
- **现象**：连段攻击时角色相对目标左右小幅横移，方向看似无规律。
- **根因**：位移全来自 root motion + MW，CMC sweep 撞目标胶囊沿切向滑开；且 warp 落点写成 `-20.f`，FVector 各分量同时减 20，站位被平移到目标世界 -XYZ 方向、与朝向脱钩。
- **修法**：落点改为沿「目标→自身」方向退 20（`FlatDir.GetSafeNormal()*20`）、Z 取自身高度；残留滑动需让 IgnoreCharacterCollision 区间覆盖带位移帧。
- **文件**：GAS/ExtraGameplayAbility.cpp、ANS_IgnoreCharacterCollision、ExtraPlayerCharacter.cpp

### Evade 多段 MW 朝向扭转失效 · 2026-09-10 · 推定解决
- **现象**：两个 EvadeFacing 区间，仅开头短区间能扭向输入，长区间左右输入不生效。
- **根因**：非同名冲突。两层原因：1. SkewWarp 的 Alpha = Delta/(EndTime-Prev)，语义为到窗口结束才对齐，长窗口每帧增量被稀释到 1–2%；2. 若窗口区间落在 BlendOut 范围内（BlendOutTriggerTime 不为 -1 时），旋转进不到窗口末尾，同样到不了预定角度。
- **修法**：长区间改 ConstantRate/SlerpWithClampedRate 并设非零 WarpMaxRotationRate，或调大 WarpRotationTimeMultiplier；同时检查窗口是否与 BlendOut 重叠。
- **文件**：Evade 蒙太奇 MW 配置；引擎 RootMotionModifier.cpp

### 变向连点停步动画不再触发 · 2026-09-09 · 推定解决
- **现象**：前→左→前再按左（或前→右→前再按前）时停步动画被打断后不重播，原地停住。
- **根因**：`StopMoveInput` 以 `IsAnyMontagePlaying` 早退；UE5.6 中被中断蒙太奇在 BlendOut 期间仍在 MontageInstances，恒为 true，故既不重播也不 RequestStop。
- **修法**：守卫只对非停步类 Montage（Evade/攻击）生效，停步/转身 Montage 即便 BlendOut 也放行。
- **文件**：ExtraPlayerCharacter.cpp（StopMoveInput）、ExtraGameAnimInstance.cpp

### 空中攻击被 Evade 打断时总是后空翻 · 2026-08-28 · 推定解决
- **现象**：空中攻击 Cancel 接 Evade 时，即使有方向输入也播放后空翻。
- **根因**：GA_Evade 用 `GetCurrentAcceleration().Size2D()` 判断方向；空中攻击 `SetMovementInputLocked(true)` 让 `Move()` 提前 return，从不调用 AddMovementInput，加速度恒为 0；即使先 EndAbility 解锁，输入加速度也要下一帧 Move() 才重建。
- **修法**：Move() 把输入标志（bHasMoveInput、按键开始时间）的更新移到锁定判断之前，只跳过 AddMovementInput；GA_Evade 改用 `HasMoveInput()` 判断。
- **文件**：ExtraPlayerCharacter.cpp、GAS/GA_Evade.cpp

---

## C. 相机与 UI

### OverheadGauge 世界模式显示异常 · 2026-09-23 · 推定解决
- **现象**：World 模式血条不固定于世界、随角色转向倾斜，俯视时被压扁；三个尺寸参数怎么改都不生效。
- **根因**：组件挂在 Mesh 下，SetRelativeRotation 的世界朝向 = Mesh 世界旋转 × LookAt，轴向错位并跟随 Yaw；CameraLocation 只在本地控制角色的 Tick 里更新，而血条配在非本地控制角色上，恒为 0；尺寸在构造函数里被消费，早于 InitProperties 把蓝图值拷入对象。
- **修法**：构造里 `SetUsingAbsoluteRotation(true)` 解绑旋转、`SetTwoSided(true)`；旋转改 SetWorldRotation 并现取本地相机、只清 Roll 保留 Pitch、删 Tick 与成员；尺寸与缩放搬到 OnConstruction，新增 OverHeadGaugeWorldScale=0.2。
- **文件**：ExtraCharacter.cpp/.h

### OverHeadWidget 血条字体颜色无法单独修改 · 2026-09-28 · 推定解决
- **现象**：头顶血条字体/颜色无法单独改，编译后回绿色。
- **根因**：ValueText 私有且 ValueTextFont 从未 SetFont；NativePreConstruct 每次 SetFillColor 覆盖 UMG 样式，父类下发早于子类。
- **修法**：C++ 不再写颜色/字体，改由 WBP 原生属性配置；删除 TeamID 红绿覆盖。
- **文件**：UI/ValueGauge、UI/OverHeadStatsGauge、ExtraCharacter

### 战斗镜头锁定与偏移问题 · 2026-09-29 · 已解决
- **现象**：镜头不跟角色转身；空居收尾跳回正后方或闪跳；GA 打断后偶发回不到原机位。
- **根因**：ClearAllRequests 从未调用；SpringArm 旋转相对父级，读组件旋转会吃角色转身漂移；ControlRotation 存基准而臂叠偏移，写回时叠了两次。
- **修法**：GA EndAbility 兜底清请求；自记录 LastBoomWorldRotation；写 ControlRotation 前减臂偏移；窗口内持续持有臂。
- **文件**：CombatCameraComponent、ANS_CombatCamera、ExtraGameplayAbility

### ValueGauge Max 为 0 警告 · 2026-09-02 · 推定解决
- **现象**：启动时 HealthBar/StaminaBar 各打印 "NewMaxValue can't be 0"，实际显示正常。
- **根因**：ListenServer 下 HUD 在 AcknowledgePossession 即绑定，Max 属性尚未复制就绪、瞬时为 0，随后 MaxValueChanged 刷正；另 bFound 复用与 Cache 无初值两处隐患。
- **修法**：拆分 bFound，仅 Max>0 才初始刷新；CacheMaxValue 初值 -1；零分母窗口内静默占位，仅曾为正值后归零才告警。
- **文件**：ValueGauge.h/.cpp

---

## D. 属性、能量与武器

### WeaponTrace 空挥误加能量 · 2026-09-12 · 已解决
- **现象**：使用 WeaponTrace 时没打到目标也会增加一次能量（+15）。
- **根因**：武器轨迹命中循环只要 Hits 非空就发 `ability.damage`；空挥扫到地形 LandscapeStreamingProxy 也被当命中，GA 侧 `HitActors.Num()==0` 判断拦不住。
- **修法**：命中循环中过滤，只对带 ASC 的目标结算并发事件，地形、场景道具跳过。
- **文件**：ExtraGameWeaponComponent.cpp、GA_Combo_Phase_2.cpp

### AirAttack 后撤三段显示组 1 武器 · 2026-09-27 · 推定解决
- **现象**：二阶段两段后接后撤再打第三段，误显示一把组 1 武器；正常三段不会。
- **根因**：`ShowWeaponEntry*` / `RequestWeaponFade` 全链路只按 Tag 查 Mesh，从不校验 `CurrentGroupTag`；切组仅隐藏不销毁 Mesh，组 1 Mesh 一旦生成，动画里跨组的 Show Notify 照样生效。
- **修法**：新增 `IsWeaponInCurrentGroup`，在 SetAttachSocket / Show / Hide / IsVisible / BeginTrails 五处入口校验，非当前组 Warning 后忽略；EndWeaponTrails 因需全局收尾不加。
- **文件**：WeaponSystem/ExtraGameWeaponComponent.h/.cpp

### GA_ComboHeavy 满段按住不触发重击 · 2026-09-11 · 推定解决
- **现象**：一直按住到最后一段仍继续普攻，必须松手重按才进重击。
- **根因**：按住时唯一触发路径是最后一段的 heavytransition AN；ComboCount 由 lastsection 的 AN 才 2→3，若 heavytransition 排在其前读到旧值判 2<3；排在 `change.*` 之后则被 Montage_JumpToSection 抢跑，永不播。
- **修法**：重排最后一段事件帧序 lastsection → heavytransition → change.<下一段>；顺序已对则加日志定位。
- **文件**：GA_ComboHeavy.cpp、GA_Combo.cpp、ExtraPlayerCharacter.cpp

---

## 未闭环（对话中断，无明确结论）

1. **ExtraArrow 销毁未即时生效**（2026-09-21）— 设为 Block 销毁，命中后停住等 Lifetime 到期才消失。会话在读投射物基类时被中断，未定位。
2. **SetTimer passed a bad function（残影）**（2026-09-17）— 疑残影组件或角色销毁后 Timer 未清，或蓝图函数改名导致委托绑定失效。未验证。
3. **头顶血条随距离变大**（2026-09-09）— Screen 空间 Widget 引擎侧按 DrawSize 恒定像素、C++ 无距离缩放，与现象矛盾；渲染路径未查完。
4. **GA_Burst01 无法由 UltAction 触发**（2026-09-04）— 主疑点为旧 BP 的 CDO 把 C++ 构造里的 AbilityTriggers 序列化覆盖为空；未验证。
5. **武器组忽略项不生效**（2026-09-01）— 过滤逻辑本身正确，推断是数据侧（未重编 / 未配置 / tag 精确匹配失败）；未验证。

---

## 非 Bug / 机制笔记

- **AiCharacter 零血回满 GE**（2026-09-02）— 功能需求，非缺陷；仅完成角色/ASC/伤害结算调研。
- **GA_Evade 前后空翻方向区分**（2026-08-30）— 需求细化：原实现只判断「是否有移动输入」，改用 `HasForwardInput()` 决定方向。
- **AN 事件与霸体 Tag 的双端同步**（2026-10-06）— `SendGameplayEventToActor → HandleGameplayEvent` 是纯本地调用，无 RPC/复制；两端一致靠 LocalPredicted GA 在两端各跑一遍蒙太奇与 AN，`AddLooseGameplayTag` 同理。沿用「单端逻辑自行 HasAuthority 门控、两端一致逻辑各自执行」。

---

## 通用经验（本轮补充）

1. **WaitInputPress 的监听要在 GA 激活的下一帧再挂载**，否则激活操作本身会直接触发回调。
2. **多段 Montage 衔接要在前一段 BlendOut 时就回调下一段播放**，实现淡入淡出重叠，否则会有一帧掉回基本状态机。
3. **多段 Montage 用枚举值标记当前状态**，因为需要区分外部打断和内部 Montage 打断（参考 GA_Skill_02）。
4. **MW 转不到预期角度**：检查窗口区间，若落在 BlendOut 范围内（BlendOutTriggerTime 不为 -1 时），会进不到窗口末尾，从而转不到预定角度。
5. **ABP 在各客户端独立求值** → RootMotion 相关 Montage 进入时机不同 → 位移不同 → 触发纠正抖动。正确做法不是各自求值，而是客户端求值，触发动画时发 ServerRPC 让服务端读值。
6. **投射物 Actor 要开 Replicate 并在权威端生成**；若由 GA 提供 Mesh，该 Mesh 也要复制。
7. **ANS 穿透的 per-play 状态不要用单槽位缓存对象对**：ANS 是全局实例，多端各调一次会互相覆盖；改成 Map 按主体分桶，退出时清空对应 Map 项的 Puller 和自身对象。
8. **惯性化停播时 BlendOutTime 会被归零**，真实时长在 `InBlendOutSettings.Blend.BlendTime`，而 `FGameplayAbilityRepAnimMontage::BlendTime` 记录的是被归零的值 → 模拟端跳变。修法：重写 `CurrentMontageStop`，惯性化且传入淡出为 0 时改用 Montage 配置值。
