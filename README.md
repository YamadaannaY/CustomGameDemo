# CustomGameDemo

## 输入 → GA 激活的链路（DS 网络同步）

单机时期输入直接派发 GameplayEvent 就能工作；拆到 DS 后必须让服务端也收到同一次输入，否则依赖输入的 GA（连段推进、二连闪、空中连打窗口等）在服务端不会有反应。下面记录旧实现的问题与现方案的完整链路。

### 旧：GameplayEvent 触发

```
【客户端】
EnhancedInput 回调 OnNormalAttackStarted
└─ SendGameplayEventToActor(InputTag.LightAttack)      ← 纯本地派发，不复制
   └─ 本端 ASC.HandleGameplayEvent
      └─ 遍历 ActivatableAbilities，匹配 GA 的 AbilityTriggers
         （TriggerSource=GameplayEvent, TriggerTag=InputTag.LightAttack）
         └─ InternalTryActivateAbility → 本地预测激活
            ├─ ActivateAbility：播 Montage + 挂 WaitGameplayEvent(InputTag.LightAttack)
            └─ LocalPredicted ⇒ 自动发 ServerTryActivateAbility(RPC)

【服务端】
ServerTryActivateAbility(RPC)
└─ 激活同一个 spec（走 CanActivateAbility 校验，不再走 trigger 匹配）
   └─ ActivateAbility：播 Montage + 挂 WaitGameplayEvent(InputTag.LightAttack)
```

**断链点**：GameplayEvent 只在调用它的那个 ASC 上派发。服务端的 GA 虽然激活了，但它挂的 `WaitGameplayEvent` 永远等不到玩家后续的输入——服务端 `NextComboName` 不被消费，Montage 停在第一段，播完 `EndAbility` 再复制结束，把客户端已经进入的第二段一起带停（表现为「只能触发第一段」）。

> 注意 GA 的**激活**一直是两端都有的：那靠 `LocalPredicted` 的预测激活 + `ServerTryActivateAbility` RPC，与事件是否复制无关。断的只是**后续输入**这条线。

### 新：InputTag 绑定（Lyra 式）

```
【授予期 · 服务端】
GiveAbility
└─ UExtraAbilitySystemComponent::ApplyAbilityInputTag
   把 GA 的 InputTag 打进 Spec.GetDynamicSpecSourceTags()

【客户端】
EnhancedInput 回调 OnNormalAttackStarted
└─ ASC.AbilityInputTagPressed(InputTag.LightAttack)    ← 只收集，不派发事件
   └─ 遍历 ActivatableAbilities，把 HasTagExact(InputTag) 的句柄收进 InputPressedSpecHandles
      └─ 帧末 AExtraPlayerController::PostProcessInput
         └─ ASC.ProcessAbilityInput()，逐个句柄：
            ├─ 未激活 → TryActivateAbility(Handle)
            │            → 本地预测激活 + ServerTryActivateAbility(RPC)
            └─ 已激活 → Spec.InputPressed = true
                        + AbilitySpecInputPressed(Spec)
                          ← 只把输入 pipe 给 GA 的 InputPressed 虚函数（Lyra 靠重写它收输入），
                            它【不】广播 GAS 的输入事件
                        + InvokeReplicatedEvent(InputPressed, Handle, PredictionKey)
                          ← 因此这里必须补一次广播（同 AbilityLocalInputPressed 的做法），
                            否则监听该事件的 WaitInputPress 永远收不到
                          ├─ 本端广播 → 本端 GA 的 WaitInputPress 触发
                          │   └─ 其 OnPress 里 ServerSetReplicatedEvent(RPC) ──→ 服务端
                          │      └─ 服务端 InvokeReplicatedEvent → 服务端 GA 的 WaitInputPress 触发 ✓
                          └─（InvokeReplicatedEvent 本身只调本地回调，不复制）

【服务端】
├─ ServerTryActivateAbility(RPC) → 激活同一个 spec
└─ ServerSetReplicatedEvent(RPC) → InvokeReplicatedEvent 广播
      └─ 服务端 GA 的 WaitInputPress 触发   ✓ 这次收到了

【各端各自（GA 内部，无网络代码）】
WaitInputPress → TryCommitCombo() → Montage_JumpToSection 跳本端那份 Montage
（Section 不参与网络复制，两端各跳各的；只要输入都到了，段就一致）
```

松手同理：`AbilityInputTagReleased` → `AbilitySpecInputReleased` → 两端 GA 的 `WaitInputRelease`（蓄力重击靠它打出结束段）。

### 核心差别

| | 旧 | 新 |
|---|---|---|
| 输入载体 | GameplayEvent | spec 上的 InputTag + GAS 输入复制通道 |
| 激活方式 | `AbilityTriggers` 匹配事件 | `ProcessAbilityInput` 里 `TryActivateAbility` |
| 服务端能否收到输入 | 不能 | 能（`EAbilityGenericReplicatedEvent::InputPressed`） |
| GA 内监听 | `WaitGameplayEvent(InputTag)` | `WaitInputPress` / `WaitInputRelease` |
| 驱动者 | 输入层直接派发 | `PlayerController::PostProcessInput` 每帧 → ASC |
| GA 内网络代码 | 无 | 无（通道在 ASC / 输入层，GA 完全不知情） |

### 两条独立通道，别混

- **输入通道**：只在客户端产生，必须靠复制同步 —— 走 `AbilityInputTagPressed/Released`。
- **动画 / 游戏事件通道**：`SendGameplayEventToActor` 仍然保留，用于 AN、GA 内部转发（连段变更帧、霸体结束、伤害帧、剑气球等）。这些由「各端各自的 Montage 播到那一帧」触发，**两端天然都会触发**，改成复制反而会重复。

### 新增能力时的注意事项

- GA 想被输入触发，必须带 `InputTag`，且**经 `GiveInitialAbilities` 或 `GrantWeaponGroupAbilities` 授予**（那里才会调 `ApplyAbilityInputTag`）。别处授予的 GA 拿不到 dynamic tag，输入会静默失效。
- GA 内监听输入一律用 `WaitInputPress` / `WaitInputRelease`。这两个 Task 是**一次性**的，收到后要在回调里重新挂载，否则监听链断开。
- 需要「在客户端产生、却要让服务端响应」的新事件，走 ASC 的输入通道；只影响本端表现的事件才用 `SendGameplayEventToActor`。
