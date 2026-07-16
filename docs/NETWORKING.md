# TinyEngine 联网指南

TinyEngine 0.26 提供可组合的传输与同步原语，不提供大厅、匹配或托管服务器。游戏层负责连接状态机，并把 UDP、可靠通道、RPC、Ownership、快照和预测组合起来。

## 1. 分层

```text
UdpSocket
  ↓ unordered datagrams, maximum 1200 bytes
ReliableChannel
  ↓ sequence / ack / 32-bit ack window / resend
RpcRegistry + NetworkOwnership
  ↓ validated gameplay messages
SnapshotCodec / SnapshotBuffer
  ↓ server state and interpolation
ClientPrediction
  ↓ local input replay and correction
ReplayStream
  ↓ offline recording using snapshot format
```

## 2. UDP

```cpp
UdpSocket socket;
if (!socket.Open(7777))
    return false;

socket.Send({"127.0.0.1", 7778}, bytes);

UdpEndpoint sender;
std::vector<std::uint8_t> packet;
while (socket.Receive(sender, packet))
{
    // Dispatch packet by protocol magic.
}
```

- 默认非阻塞。
- 当前只接受 IPv4 文本地址。
- 空包、无效地址、零端口和超过 1200 字节的 Datagram 被拒绝。
- Windows 使用 Winsock，其他平台使用 BSD Socket。

## 3. 可靠通道

```cpp
ReliableChannel channel;
channel.Queue(payload);

for (const auto &packet : channel.BuildPackets(nowSeconds))
    socket.Send(peer, packet);
```

接收：

```cpp
std::vector<std::uint8_t> payload;
if (channel.ProcessPacket(packet, nowSeconds, payload))
    HandleReliablePayload(payload);
```

通道 Header 包含 Magic、Sequence、Ack、Ack Bits 和 Payload Size。重复包和窗口外旧包不会再次交付；未确认包按重发间隔再次输出。队列最多 1024 项，每项最多尝试 32 次。

Ack 需要反向流量携带。单向应用应定期排队心跳或 Ack 消息，否则发送端无法确认完成。

## 4. RPC

RPC 必须先注册白名单 ID：

```cpp
RpcRegistry rpc;
rpc.Register(10, [](std::uint32_t clientId,
                    const std::vector<std::uint8_t> &payload) {
    // Validate gameplay arguments again before changing world state.
});

const auto packet = rpc.Encode(10, payload);
rpc.Dispatch(clientId, packet, nowSeconds);
```

安全边界：

- 未注册 ID 不能编码或分发。
- Payload 上限 1024 字节。
- Header Size 必须与实际包长完全一致。
- 每个 Client 使用独立 Token Bucket 限流。
- Handler 执行前只完成协议校验；坐标、实体状态、冷却等业务约束仍由游戏验证。

## 5. Ownership

```cpp
NetworkOwnership ownership;
ownership.Assign(entity, clientId);

if (!ownership.IsOwner(entity, clientId))
    return; // Reject client mutation.
```

已有 Owner 的实体不能被其他 Client 覆盖或释放。客户端断开时调用 `RemoveClient` 清理所有权。

## 6. 客户端预测与服务器纠错

```cpp
ClientPrediction prediction;
prediction.Reset(spawnPosition);

PredictedInput input{sequence, movementPerSecond, deltaTime};
const Vec3 predicted = prediction.ApplyInput(input);

const Vec3 corrected = prediction.Reconcile(
    serverAckedInput,
    authoritativePosition,
    5.0f);
```

- 输入 Sequence 必须严格递增。
- 单步 `deltaTime` 限制为 0–0.25 秒。
- 历史最多保存 256 条。
- Reconcile 删除已确认输入，从权威位置重放其余输入。
- 小误差做 50% 平滑，大于 Snap Distance 的误差直接采用重放结果。

预测器只处理 Vec3 位移示例。旋转、跳跃、碰撞和能力状态需要游戏定义确定性输入与重放函数。

## 7. 快照与回放

`SnapshotCodec` 保存 Sequence、服务器时间与实体 Position/Rotation/Velocity。`SnapshotBuffer` 最多保留 64 帧，并在相邻服务器时间之间插值。`ReplayStream` 复用同一快照编码，避免在线和离线状态出现两套语义。

## 8. 仍需由产品层补充

- 连接握手、超时、心跳和重连状态机。
- 身份认证、加密、密钥轮换和防重放。
- NAT 穿透、中继、会话、匹配和服务器发现。
- Datagram 分片、拥塞控制和带宽优先级。
- 大规模 Replication Graph、Interest Management 和 Delta Compression。
- 权威物理、延迟补偿、作弊检测和服务端审计。

因此当前网络模块可用于局域网原型和同步机制验证，但在公开互联网发布前必须补齐认证、加密和连接层安全。
