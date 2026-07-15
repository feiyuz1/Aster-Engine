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

class SnapshotCodec
{
  public:
    static std::vector<std::uint8_t> Encode(const NetworkSnapshot &snapshot);
    static bool Decode(const std::vector<std::uint8_t> &bytes, NetworkSnapshot &snapshot);
};

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
