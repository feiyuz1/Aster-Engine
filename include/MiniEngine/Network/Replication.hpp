// 文件：include/MiniEngine/Network/Replication.hpp
// 作用：实现快照复制、可靠通道、RPC、所有权、预测与服务器纠错。
// 约定：网络输入均视为不可信数据，解码前先验证长度和权限。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct ReplicatedEntityState
{
    Entity entity = InvalidEntity;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 velocity{};
};

struct NetworkSnapshot
{
    std::uint32_t sequence = 0;
    double timestamp = 0.0;
    std::vector<ReplicatedEntityState> entities;
};

// 确定性快照编解码器；解码任何变长字段前都必须验证剩余字节数和数量上限。
class SnapshotCodec
{
  public:
    /// @brief 将输入数据编码为目标格式。
    /// @param snapshot 要保存、发送或应用的状态快照。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<std::uint8_t> Encode(const NetworkSnapshot &snapshot);
    /// @brief 解码输入数据并验证其有效性。
    /// @param bytes 要处理的字节数据或字节数量。
    /// @param snapshot 要保存、发送或应用的状态快照。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Decode(const std::vector<std::uint8_t> &bytes, NetworkSnapshot &snapshot);
};

// 按服务器时间排序的抖动缓冲区，通过延迟少量显示时间换取稳定插值。
class SnapshotBuffer
{
  public:
    /// @brief 把数据压入队列或缓冲区。
    /// @param snapshot 要保存、发送或应用的状态快照。
    void Push(NetworkSnapshot snapshot);
    /// @brief 在指定位置或时间采样数据。
    /// @param timestamp 服务器或回放时间戳，单位为秒。
    /// @param states 接收采样后实体状态的输出集合。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Sample(double timestamp, std::vector<ReplicatedEntityState> &states) const;
    /// @brief 清空对象保存的状态和数据。
    void Clear();
    /// @brief 获取缓冲区中的快照数量。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t Size() const;

  private:
    std::vector<NetworkSnapshot> m_snapshots;
};

// 复用快照格式的离线回放流；文件版本不兼容时应明确失败而不是猜测布局。
class ReplayStream
{
  public:
    /// @brief 记录一帧回放快照。
    /// @param snapshot 要保存、发送或应用的状态快照。
    void Record(NetworkSnapshot snapshot);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 在指定位置或时间采样数据。
    /// @param timestamp 服务器或回放时间戳，单位为秒。
    /// @param states 接收采样后实体状态的输出集合。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Sample(double timestamp, std::vector<ReplicatedEntityState> &states) const;
    /// @brief 清空对象保存的状态和数据。
    void Clear();
    /// @brief 获取 `Duration`。
    /// @return 返回计算得到的数值、数量或索引。
    double GetDuration() const;

  private:
    SnapshotBuffer m_buffer;
    std::vector<NetworkSnapshot> m_frames;
};

constexpr std::size_t NetworkMaximumDatagram = 1200;
constexpr std::size_t NetworkMaximumRpcPayload = 1024;

struct UdpEndpoint
{
    std::string address = "127.0.0.1";
    std::uint16_t port = 0;
};

// UDP 系统句柄的 RAII 封装；Open 成功后由析构或 Close 唯一释放。
class UdpSocket
{
  public:
    /// @brief 构造 `UdpSocket` 对象。
    UdpSocket();
    /// @brief 析构 `UdpSocket` 对象并释放其持有资源。
    ~UdpSocket();
    /// @brief 构造 `UdpSocket` 对象。
    UdpSocket(const UdpSocket &) = delete;
    /// @brief 实现 `=` 运算符语义。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UdpSocket &operator=(const UdpSocket &) = delete;
    /// @brief 打开资源或服务。
    /// @param port 接口所需的输入参数。
    /// @param nonBlocking 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Open(std::uint16_t port = 0, bool nonBlocking = true);
    /// @brief 关闭资源或服务。
    void Close();
    /// @brief 向指定端点发送 UDP 数据报。
    /// @param endpoint 网络 IP 地址与端口端点。
    /// @param data 要读取、写入或发送的数据。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Send(const UdpEndpoint &endpoint, const std::vector<std::uint8_t> &data) const;
    /// @brief 接收一个 UDP 数据报及其来源端点。
    /// @param endpoint 网络 IP 地址与端口端点。
    /// @param data 要读取、写入或发送的数据。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Receive(UdpEndpoint &endpoint, std::vector<std::uint8_t> &data);
    /// @brief 获取 `BoundPort`。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint16_t GetBoundPort() const;
    /// @brief 判断是否满足 `Open` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsOpen() const;

  private:
    std::uintptr_t m_socket = static_cast<std::uintptr_t>(-1);
    std::uint16_t m_boundPort = 0;
};

// 在 UDP 上维护序列号、确认位图和超时重发；可靠仅保证送达，不保证业务幂等。
class ReliableChannel
{
  public:
    /// @brief 把数据加入待处理队列。
    /// @param payload 要发送、接收或分发的消息载荷。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Queue(std::vector<std::uint8_t> payload);
    /// @brief 构建 `Packets`。
    /// @param nowSeconds 当前单调时钟时间，单位为秒。
    /// @param resendSeconds 时间值，单位按接口约定为秒。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::vector<std::uint8_t>> BuildPackets(double nowSeconds, double resendSeconds = 0.1);
    /// @brief 处理 `Packet`。
    /// @param packet 网络数据包字节。
    /// @param nowSeconds 当前单调时钟时间，单位为秒。
    /// @param payload 要发送、接收或分发的消息载荷。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ProcessPacket(const std::vector<std::uint8_t> &packet, double nowSeconds, std::vector<std::uint8_t> &payload);
    /// @brief 获取待处理 `Count`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t PendingCount() const;
    /// @brief 获取 `LastReceivedSequence`。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint32_t GetLastReceivedSequence() const;

  private:
    struct Pending
    {
        std::uint32_t sequence = 0;
        std::vector<std::uint8_t> payload;
        double lastSent = -1.0;
        std::uint32_t attempts = 0;
    };
    /// @brief 应用 `Acks`。
    /// @param ack 接口所需的输入参数。
    /// @param ackBits 接口所需的输入参数。
    void ApplyAcks(std::uint32_t ack, std::uint32_t ackBits);
    std::deque<Pending> m_pending;
    std::uint32_t m_nextSequence = 1;
    std::uint32_t m_remoteSequence = 0;
    std::uint32_t m_receivedBits = 0;
};

// 令牌桶式流量限制，所有来自远端的消息应在解析高成本载荷前通过此检查。
class NetworkRateLimiter
{
  public:
    /// @brief 构造 `NetworkRateLimiter` 对象。
    /// @param ratePerSecond 本次操作使用的数值。
    /// @param burst 接口所需的输入参数。
    NetworkRateLimiter(double ratePerSecond = 60.0, double burst = 120.0);
    /// @brief 根据令牌桶状态判断并消耗一次请求额度。
    /// @param nowSeconds 当前单调时钟时间，单位为秒。
    /// @param cost 导航或查询使用的代价值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Allow(double nowSeconds, double cost = 1.0);

  private:
    double m_rate = 60.0;
    double m_capacity = 120.0;
    double m_tokens = 120.0;
    double m_lastTime = -1.0;
};

// RPC 白名单与分发表。未登记、尺寸超限或无权限调用必须在触达游戏逻辑前拒绝。
class RpcRegistry
{
  public:
    using Handler = std::function<void(std::uint32_t clientId, const std::vector<std::uint8_t> &payload)>;
    /// @brief 注册对象或处理器。
    /// @param id 目标对象、消息或任务标识符。
    /// @param handler 收到事件或 RPC 时调用的处理器。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Register(std::uint16_t id, Handler handler);
    /// @brief 将输入数据编码为目标格式。
    /// @param id 目标对象、消息或任务标识符。
    /// @param payload 要发送、接收或分发的消息载荷。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::uint8_t> Encode(std::uint16_t id, const std::vector<std::uint8_t> &payload) const;
    /// @brief 分派待处理事件或任务。
    /// @param clientId 网络客户端标识符。
    /// @param packet 网络数据包字节。
    /// @param nowSeconds 当前单调时钟时间，单位为秒。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Dispatch(std::uint32_t clientId, const std::vector<std::uint8_t> &packet, double nowSeconds);

  private:
    std::unordered_map<std::uint16_t, Handler> m_handlers;
    std::unordered_map<std::uint32_t, NetworkRateLimiter> m_limiters;
};

// 记录实体的网络所有者；客户端上行修改只能作用于其拥有的实体。
class NetworkOwnership
{
  public:
    /// @brief 把实体的网络控制权分配给客户端。
    /// @param entity 目标实体句柄。
    /// @param clientId 网络客户端标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Assign(Entity entity, std::uint32_t clientId);
    /// @brief 释放对象持有的外部资源。
    /// @param entity 目标实体句柄。
    /// @param clientId 网络客户端标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Release(Entity entity, std::uint32_t clientId);
    /// @brief 判断是否满足 `Owner` 条件。
    /// @param entity 目标实体句柄。
    /// @param clientId 网络客户端标识符。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsOwner(Entity entity, std::uint32_t clientId) const;
    /// @brief 移除 `Client`。
    /// @param clientId 网络客户端标识符。
    void RemoveClient(std::uint32_t clientId);

  private:
    std::unordered_map<Entity, std::uint32_t> m_owners;
};

struct PredictedInput
{
    std::uint32_t sequence = 0;
    Vec3 movement{};
    float deltaTime = 0.0f;
};

// 保存未确认输入并在服务器权威状态到达时回滚、重放，缩小本地交互延迟。
class ClientPrediction
{
  public:
    /// @brief 将对象恢复为初始状态。
    /// @param position 世界空间或局部空间位置。
    void Reset(const Vec3 &position = {});
    /// @brief 应用 `Input`。
    /// @param input 接口所需的输入参数。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 ApplyInput(const PredictedInput &input);
    /// @brief 根据服务器权威位置校正并重放未确认输入。
    /// @param acknowledgedInput 接口所需的输入参数。
    /// @param authoritativePosition 相关坐标或空间位置。
    /// @param snapDistance 本次操作使用的数值。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 Reconcile(std::uint32_t acknowledgedInput, const Vec3 &authoritativePosition, float snapDistance = 5.0f);
    /// @brief 获取 `Position`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const Vec3 &GetPosition() const;
    /// @brief 获取待处理 `InputCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t PendingInputCount() const;

  private:
    Vec3 m_position{};
    std::deque<PredictedInput> m_inputs;
};
} // 结束 MiniEngine 命名空间
