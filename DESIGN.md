# ESMP — Endless Sky 局域网联机

本分支为 Endless Sky 增加"主机建房 + 客户端加入"的局域网联机功能。
设计依据见本文档；协议细节见 `source/network/Protocol.h`。

## 结论：主机权威（Host Authoritative）

放弃确定性锁步，理由（代码证据）：

- RNG 为 thread_local 的 mt19937_64，主/worker 线程各一份且消费顺序不定（`Random.cpp:35-38`），全库 `Random::` 消费 176 处，覆盖弹道散布、命中判定、NPC 生成、经济波动；
- 模拟在 worker 线程跑（`Engine::Go` → `CalculateStep`），主线程做输入与渲染（双缓冲，绘制落后一帧）；
- 60FPS 帧驱动准固定步长（`main.cpp:320` FrameTimer(60)、`main.cpp:486` StepAll），快进 = 每帧 3 tick。

主机照常跑完整模拟；客户端本地照常跑自己的世界，远端玩家舰船以"幽灵船"注入本地
`Engine::ships`（`Engine.h:240`），由网络快照驱动 + 插值渲染；自己的输入 30Hz 上行。
世界状态按星系存储（ships/projectiles/flotsam 均为当前星系列表）→ 快照只含同星系实体，
跨星系时流量趋近于零。

## 传输层

| 通道 | 端口 | 用途 |
|------|------|------|
| TCP 4777 | 可靠 | 握手、聊天、开火/命中事件、星系 ID 表、跳跃/降落 |
| UDP 4777 | 不可靠 | 30Hz 位置快照、输入上行、局域网发现广播 |

主机每秒广播一次 BEACON（UDP 255.255.255.255:4777），大厅自动列出主机，也可直填 IP。

## 协议 v1（详见 Protocol.h）

- C→H：HELLO{版本,名字} → INPUT{Command/FireCommand 位集,鼠标目标} 30Hz
- H→C：WELCOME{slot,主机名} → SYS_TABLE{系统名↔id 表} → SNAPSHOT{seq,实体数组} 30Hz
- 事件：FIRE_EVENT / SHIP_EVENT（镜像 Engine::ShipEvent）/ SYS_CHANGE / LANDED
- 双向 PING/PONG 测延迟

## 改动落点

新增 `source/network/`：NetSocket（跨平台 socket 封装）、Protocol（线协议）、
NetServer / NetClient（会话逻辑）、LobbyPanel（大厅 UI，Panel 子类挂 menuPanels，
入口照抄 MenuPanel 的按键模式 `MenuPanel.cpp:203-218`）。

改现有文件：

- `Engine.cpp/h`：Step 后挂 NetServer::Tick / NetClient::Tick；注入幽灵船到 `Engine::ships`；
  幽灵船绕过 AI（`AI::Step` 跳过），由快照插值驱动；客户端给幽灵船设置远端玩家的
  `SetCommands`（`Ship.cpp:2094`），与本地玩家在 `Ship::commands → Ship::Move` 汇合。
- `MenuPanel.cpp`：大厅入口。
- `Ship`：幽灵船标志（不写存档、不参与 AI、不上传快照）。

净增约 900–1500 行，触碰现有文件 6–10 个。

## 里程碑

1. **M1 双船互见**：大厅建房/加入/列表 + 两台机器同星系看到对方舰船移动（快照+插值）。
2. **M2 双人战斗**：开火事件转发，主机权威结算伤害，SHIP_EVENT 回传驱动爆炸特效。
3. **M3 共享世界**：客户端关闭本地舰队生成（`Engine::SpawnFleets/SpawnPersons`），
   渲染主机快照中的 NPC 舰队，遭遇战一致。
4. **M4 流程同步**：跳跃/降落/会合同步；客户端存档剥离幽灵船；暂停/快进联动。

## 已知取舍（M4 前接受）

- 客户端与主机的经济/任务/宇宙事件各自演化，不同步（各自玩各自的"单机宇宙"，
  共享的只有同星系内的人和弹）；
- 幽灵船通过舰船模型名等静态数据重建，不传指针；数据版本不一致时握手直接拒绝；
- 主机暂停 = 全员暂停；快进仅主机可用。

## 局域网实测验证方案（M1）

### 准备

- 两台设备接入**同一 Wi-Fi / 热点**；性能较好的一台当主机。
- 两边都用完整检出（运行游戏需要 `data/`、`images/` 等资源，稀疏检出无法游玩）：

  ```
  git clone https://github.com/<your-fork>/endless-sky.git
  cd endless-sky && git checkout lan-multiplayer
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
  ```

- 确认两端 `git log` 一致（数据版本不同会在 WELCOME 阶段拒连——版本号取自 GameVersion）。

### 步骤

1. **主机**：启动游戏 → 主菜单 → 局域网大厅（`L` 键）→「开始建房」→ 正常开始/载入游戏。
2. **客户端**：启动游戏 → 局域网大厅 → 列表应自动出现主机（UDP 广播发现，约 1 秒一跳）→ 选中加入；也可直填主机 IP。
3. 双方各自开始新游戏（默认起点相同，天然同星系）。

### 验证清单

| # | 验证点 | 预期 |
|---|--------|------|
| 1 | 大厅发现 | 客户端列表出现主机名与人数 |
| 2 | 握手 | 客户端显示 slot 分配与主机名；RTT（PING/PONG）打印在 errors.txt |
| 3 | 幽灵船出现 | 同星系内看到对方的船（贴图为对方旗舰型号） |
| 4 | 移动同步 | 对方船的位置/转向随其操控平滑变化（30Hz 快照） |
| 5 | 跨星系隔离 | 任一方跳跃后，另一方幽灵消失；回到同星系自动重现 |
| 6 | 断线清理 | 客户端退出后，主机的幽灵船移除、人数回落 |
| 7 | 主机权威 | 客户端看到的对方船行为与其输入一致（转向/推进） |

### M1 已知限制（记录在案，M2+ 解决）

- 客户端本地也可以打到幽灵船并看到爆炸，但主机侧的伤害结算在 M2 才做事件转发；
- 主机暂停/快进尚未联动客户端；
- 副武器开火、炮塔瞄准精度在 M2 接入 FireCommand 完整位集；
- 经济/任务/宇宙事件两端各自演化（见"已知取舍"）。
