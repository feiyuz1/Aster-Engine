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
    static std::vector<std::uint8_t> Encode(const NetworkSnapshot &snapshot);
    static bool Decode(const std::vector<std::uint8_t> &bytes, NetworkSnapshot &snapshot);
};

// 按服务器时间排序的抖动缓冲区，通过延迟少量显示时间换取稳定插值。
class SnapshotBuffer
{
  public:
    void Push(NetworkSnapshot snapshot);
    bool Sample(double timestamp, std::vector<ReplicatedEntityState> &states) const;
    void Clear();
    std::size_t Size() const;

  private:
    std::vector<NetworkSnapshot> m_snapshots;
};

// 复用快照格式的离线回放流；文件版本不兼容时应明确失败而不是猜测布局。
class ReplayStream
{
  public:
    void Record(NetworkSnapshot snapshot);
    bool Save(const std::filesystem::path &path) const;
    bool Load(const std::filesystem::path &path);
    bool Sample(double timestamp, std::vector<ReplicatedEntityState> &states) const;
    void Clear();
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
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;
    bool Open(std::uint16_t port = 0, bool nonBlocking = true);
    void Close();
    bool Send(const UdpEndpoint &endpoint, const std::vector<std::uint8_t> &data) const;
    bool Receive(UdpEndpoint &endpoint, std::vector<std::uint8_t> &data);
    std::uint16_t GetBoundPort() const;
    bool IsOpen() const;

  private:
    std::uintptr_t m_socket = static_cast<std::uintptr_t>(-1);
    std::uint16_t m_boundPort = 0;
};

// 在 UDP 上维护序列号、确认位图和超时重发；可靠仅保证送达，不保证业务幂等。
class ReliableChannel
{
  public:
    bool Queue(std::vector<std::uint8_t> payload);
    std::vector<std::vector<std::uint8_t>> BuildPackets(double nowSeconds, double resendSeconds = 0.1);
    bool ProcessPacket(const std::vector<std::uint8_t> &packet, double nowSeconds, std::vector<std::uint8_t> &payload);
    std::size_t PendingCount() const;
    std::uint32_t GetLastReceivedSequence() const;

  private:
    struct Pending
    {
        std::uint32_t sequence = 0;
        std::vector<std::uint8_t> payload;
        double lastSent = -1.0;
        std::uint32_t attempts = 0;
    };
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
    NetworkRateLimiter(double ratePerSecond = 60.0, double burst = 120.0);
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
    bool Register(std::uint16_t id, Handler handler);
    std::vector<std::uint8_t> Encode(std::uint16_t id, const std::vector<std::uint8_t> &payload) const;
    bool Dispatch(std::uint32_t clientId, const std::vector<std::uint8_t> &packet, double nowSeconds);

  private:
    std::unordered_map<std::uint16_t, Handler> m_handlers;
    std::unordered_map<std::uint32_t, NetworkRateLimiter> m_limiters;
};

// 记录实体的网络所有者；客户端上行修改只能作用于其拥有的实体。
class NetworkOwnership
{
  public:
    bool Assign(Entity entity, std::uint32_t clientId);
    bool Release(Entity entity, std::uint32_t clientId);
    bool IsOwner(Entity entity, std::uint32_t clientId) const;
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
    void Reset(const Vec3 &position = {});
    Vec3 ApplyInput(const PredictedInput &input);
    Vec3 Reconcile(std::uint32_t acknowledgedInput, const Vec3 &authoritativePosition, float snapDistance = 5.0f);
    const Vec3 &GetPosition() const;
    std::size_t PendingInputCount() const;

  private:
    Vec3 m_position{};
    std::deque<PredictedInput> m_inputs;
};
} // namespace MiniEngine
