# ExtractGameCharacter — 工作区指令（DeepSeek Harness）

> 派生自仓库外的 `CLAUDE.md`（原先 Claude Code 的协作约束）与 `Docs/架构文档（复习用）.md`。
> 本文件在每个会话首轮自动注入，改动会影响后续所有会话；如需同步，请连同 `../../CLAUDE.md` 一起改。
> 优先级：本节 > `Docs/` > 代码注释。文档与代码冲突时以代码为准，并说明是哪一段过期了。

## 1. 协作规则

- 思考与回复一律用**中文**。
- **默认精简**：代码改完给「改动文件 + 一句要点」，不写长篇解释；解释类问题简明回答，被要求展开时才展开。
- 不复述已拍板的结论。`Docs/架构文档（复习用）.md`、`BugList.md` 已有的机制说明直接引用章节号，不重抄。
- **不代编译**：不要运行 UnrealBuildTool、`Build.bat`、Live Coding 或 UnrealEditor。改完只声明「请编译测试」，由用户编译，报错贴回后再修。
  - 可供参照的路径：引擎 `D:\UE\UE_5.6`，工程 `E:\UEProject\ExtractGameCharacter\ExtractGameCharacter.uproject`。核对引擎 API 时**可以读**引擎源码，但不要构建。
- 涉及**资产侧**配置（Montage、AN/ANS 面板参数、GA Class Defaults、DataAsset/DataTable、Curve）时，除改代码外必须点明「编辑器里要改哪一项」，这类改动无法只靠源码完成。
- 上下文压缩由 Harness 自动完成，无需手动管理；不要编造 token 用量数字。
- 改共享基类（`ExtraAbilityBase`、`ExtraGameplayAbility`、`ExtraAbilitySystemComponent`、`ExtraCharacter`、`ExtraGameMovementComponent`）前，先列出受影响的子类/调用方再动手；这类改动一律按「全量 GA 都会受影响」对待。

## 2. 项目速览

- UE 5.6，单运行时模块 `ExtractGameCharacter`；本目录（`Source/ExtractGameCharacter`）就是 git 根。
- 玩法：《鸣潮》「绯雪」技能组的 GAS 战斗复刻 Demo；DS 权威 + 客户端预测，134 个 `.cpp/.h`。
- 依赖模块见 `ExtractGameCharacter.Build.cs`：GameplayAbilities / GameplayTags / MotionWarping / EnhancedInput / UMG / Niagara / EngineCameras。
- 资产在 UE 工程根 `../../Content/Wuwa/FeiXue/`（Animation / Camera / Curve / Gameplay / Weapon）；命名空间 `/Game/Wuwa/FeiXue/...`。数值与表现主要配在 DA/DT/Montage 面板上，代码不硬编码资产路径。

## 3. 代码地图

| 目录 | 职责 | 主要文件 |
|---|---|---|
| 根 | 角色、控制器、AnimInstance、移动组件、ASC 静态库 | `ExtraCharacter`、`ExtraPlayerCharacter`、`ExtraPlayerController`、`ExtraGameAnimInstance`、`ExtraGameMovementComponent`、`ExtraCharacterNetworkMoveData.h`、`UExtraAbilitySystemStatic` |
| `GAS/` | 基类、技能、属性/GE 调用、伤害飘字 Cue | `ExtraAbilityBase`、`ExtraGameplayAbility`、`ExtraAbilitySystemComponent`、`ExtraPassiveGameplayAbility`、`GA_*`、`GameplayCueNotify_DamageNumber` |
| `AN/` | AnimNotify（瞬时）与 AnimNotifyState（区间），时间线上的逻辑分发 | `AN_SendGameplayEvent`、`AN_CancelWindow`、`ANS_WeaponTrace`、`ANS_CombatCamera`、`ANS_AudioEvent`、`AN_AreaCheck` |
| `WeaponSystem/` | 武器组 DA、武器组件（Mesh/显隐/Trace 扫描）、属性集 | `ExtraGameWeaponData`、`ExtraGameWeaponComponent`、`ExtraGameAttributeSet` |
| `Camera/` | 战斗相机请求栈、曲线震动 | `UCombatCameraComponent`、`ExtraCurveCameraShake(Pattern)` |
| `LockOn/` | 锁定检测与目标复制 | `ULockOnComponent` |
| `Projectile/` | 投射物基类、箭矢、剑气 | `ExtraProjectile`、`ExtraArrow`、`ExtraSwordQi` |
| `UI/` | HUD、血条/能量条/计数条、飘字 Actor | `GameplayWidget`、`ValueGauge`、`CountGauge`、`OverHeadStatsGauge`、`DamageNumberActor` |
| `AI/` | 木桩 AI 角色（自行初始化 GAS，不装备武器组） | `ExtraAICharacter` |

## 4. 不变量（改代码前先确认没破坏）

1. **服务端权威**：伤害、锁定、GA 授予、GE、轨迹扫描、范围判定只在权威端执行；Mesh/Montage/相机/音频/飘字由各端本地生成。
2. **动画与 GA 解耦**：GA 与动画之间只通过 `GameplayEvent + TargetData` 通信，不直接调用。
3. **Montage Section 不复制**：连段由各端凭同一次输入各自跳段；不要为 Section 加同步。
4. **Montage 停止只走 ASC**（`CurrentMontageStop` / `StopMontageIfCurrent`）。直连 AnimInstance 会让模拟端不同步、丢补帧。
5. **LooseTag 不复制**：形态 Tag、`State.CancelWindow`、`State.JuheReady` 等需各端自行补齐或由 RPC 驱动。
6. **服务端骨骼姿势必须刷新**：`AlwaysTickPoseAndRefreshBones = true` 不能改，否则武器 Trace 扫的是冻结姿势。
7. **击飞不能只在服务端推**：客户端 ServerMove 会覆盖，必须 `MulticastLaunchCharacter`。
8. **新 GA 要被输入触发**：必须带 `InputTag`，且经 `GiveInitialAbilities` / `GrantWeaponGroupAbilities` 授予（那里才调 `ApplyAbilityInputTag`）；GA 内监听用 `WaitInputPress/Release`，两者是**一次性**的，回调里要重新挂载。
9. **两条通道别混**：客户端产生但要服务端响应的事件走 ASC 输入通道；只影响本端表现的才用 `SendGameplayEventToActor`。
10. **AN/ANS 的 Tag 由子类覆写**（`GetEventTag` / `GetWindowBeginTag` / `GetWindowEndTag`），面板隐藏，不要改成手填。
11. **`EndAbility` 的收尾顺序有依赖**（架构文档 §4.2），往里面加逻辑前先读那段顺序。
12. **相机请求是意图不是最终值**：`bModifyXxx` 控制通道覆盖，出栈后统一淡回基准，`ClearAllRequests` 是 GA 的兜底。

## 5. 文档索引（按需分段读，别一次全读）

- `Docs/架构文档（复习用）.md`（约 1000 行，最后更新 2026-11-10）——按主题跳读：
  §1 总览/初始化时序 · §2 输入管线 · §3 武器与 Trace · §4 GA 基类与连段 · §5 伤害/范围/投射物/能量/飘字 ·
  §6 动画与停步 · §7 移动与旋转 · §8 锁定与 MotionWarping · §9 相机 · §10 音频 · §11 UI · §12 数据驱动总表 ·
  §13 DS 复制总表与约束 · §14 新增技能/武器组/镜头/音效检查清单。
  注意：其「核心目录」表里的 `Debug/` 已删除（2026-11-11 那次提交），§7 的移动/网络移动数据部分早于当前未提交改动，涉及这两处以代码为准。
- `README.md`——输入 → GA 激活的 DS 链路（Lyra 式 InputTag 绑定）与新增能力注意事项。
- `BugList.md`——33 条历史 Bug 的根因与修法（A 类网络同步与 GAS、后续为动画/相机等）。改网络、动画、GA 前先扫同类条目，避免重复踩。
- 需要更多上下文时直接读源码；不要凭架构文档的旧描述改代码。

## 6. 常见任务入口

| 任务 | 先读 | 主要落点 |
|---|---|---|
| 新增技能/连段 | §4.2、§14，参照 `GA_Combo` | 新建 `GA_*` 类 + Montage（AN/ANS 见 §4.3） |
| 伤害数值/多段/能量不对 | §5.2、§5.3 | GA Class Defaults 的 `AttackSegmentConfigs` |
| 联机不同步/客户端没表现 | §13、`BugList.md` A 类 | 复制属性、RPC、LooseTag 补齐 |
| 停步/转身/急停异常 | §6.3、§6.4 | `ExtraGameAnimInstance`、`ExtraPlayerCharacter` |
| 移动手感/转向/冲刺 | §7 | `ExtraGameMovementComponent` + Curve 资产 |
| 战斗镜头参数 | §9 | `ANS_CombatCamera` 上的 `FCombatCameraRequest` |
| 武器挂点/显隐/换形态 | §3.2、§3.3 | `ExtraGameWeaponData`（DA） + `ExtraGameWeaponComponent` |
| 锁定/追踪距离 | §8 | `ULockOnComponent`、GA 的 warp 参数 |
