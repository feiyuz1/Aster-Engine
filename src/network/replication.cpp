// 文件：src/network/replication.cpp
// 作用：实现快照复制、可靠通道、RPC、所有权、预测与服务器纠错。
// 约定：网络输入均视为不可信数据，解码前先验证长度和权限。
// 类型：实现文件。

#include "tinyengine/network/replication.h"

#include "tinyengine/platform/platform.h"
#include "tinyengine/serialization/archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tinyengine
{
namespace
{
void writeState(BinaryWriter& writer, const ReplicatedEntityState& state)
{
    writer.writeU64(state.entity);
    for (const Vec3 value : {state.position, state.rotation, state.velocity})
    {
        writer.writeFloat(value.x);
        writer.writeFloat(value.y);
        writer.writeFloat(value.z);
    }
}

bool readState(BinaryReader& reader, ReplicatedEntityState& state)
{
    return reader.readU64(state.entity) && reader.readFloat(state.position.x) && reader.readFloat(state.position.y) &&
           reader.readFloat(state.position.z) && reader.readFloat(state.rotation.x) &&
           reader.readFloat(state.rotation.y) && reader.readFloat(state.rotation.z) &&
           reader.readFloat(state.velocity.x) && reader.readFloat(state.velocity.y) &&
           reader.readFloat(state.velocity.z);
}
} // namespace

std::vector<std::uint8_t> SnapshotCodec::encode(const NetworkSnapshot& snapshot)
{
    BinaryWriter writer;
    writer.writeBytes("TNYNET1\0", 8);
    writer.writeU32(snapshot.sequence);
    std::uint64_t timestampBits = 0;
    static_assert(sizeof(timestampBits) == sizeof(snapshot.timestamp));
    std::memcpy(&timestampBits, &snapshot.timestamp, sizeof(timestampBits));
    writer.writeU64(timestampBits);
    writer.writeU32(static_cast<std::uint32_t>(std::min<std::size_t>(snapshot.entities.size(), 65536)));
    for (std::size_t index = 0; index < snapshot.entities.size() && index < 65536; ++index)
        writeState(writer, snapshot.entities[index]);
    return writer.GetData();
}

// 先解码到临时对象并完成全部边界检查，成功后再替换输出，避免半解析快照污染调用方状态。
bool SnapshotCodec::decode(const std::vector<std::uint8_t>& bytes, NetworkSnapshot& snapshot)
{
    if (bytes.size() < 24 || !std::equal(bytes.begin(), bytes.begin() + 8, "TNYNET1\0"))
        return false;
    BinaryReader reader(bytes);
    char magic[8];
    std::uint64_t timestampBits = 0;
    std::uint32_t count = 0;
    if (!reader.readBytes(magic, sizeof(magic)) || !reader.readU32(snapshot.sequence) ||
        !reader.readU64(timestampBits) || !reader.readU32(count) || count > 65536)
        return false;
    std::memcpy(&snapshot.timestamp, &timestampBits, sizeof(timestampBits));
    snapshot.entities.assign(count, {});
    for (auto& state : snapshot.entities)
        if (!readState(reader, state))
            return false;
    return true;
}

void SnapshotBuffer::push(NetworkSnapshot snapshot)
{
    const auto existing =
        std::find_if(m_snapshots.begin(), m_snapshots.end(),
                     [&](const NetworkSnapshot& value) { return value.sequence == snapshot.sequence; });
    if (existing != m_snapshots.end())
        *existing = std::move(snapshot);
    else
        m_snapshots.push_back(std::move(snapshot));
    std::sort(m_snapshots.begin(), m_snapshots.end(),
              [](const NetworkSnapshot& left, const NetworkSnapshot& right)
              { return left.timestamp < right.timestamp; });
    if (m_snapshots.size() > 64)
        m_snapshots.erase(m_snapshots.begin(), m_snapshots.end() - 64);
}

// 在包围目标时间的两个快照间插值；实体只存在于一侧时采用临近权威状态而不凭空外推。
bool SnapshotBuffer::sample(const double timestamp, std::vector<ReplicatedEntityState>& states) const
{
    if (m_snapshots.empty())
        return false;
    const auto right =
        std::lower_bound(m_snapshots.begin(), m_snapshots.end(), timestamp,
                         [](const NetworkSnapshot& snapshot, const double time) { return snapshot.timestamp < time; });
    if (right == m_snapshots.begin() || right == m_snapshots.end())
    {
        states = (right == m_snapshots.begin() ? m_snapshots.front() : m_snapshots.back()).entities;
        return true;
    }
    const auto &before = *(right - 1), &after = *right;
    const float alpha = after.timestamp > before.timestamp
                            ? static_cast<float>(std::clamp(
                                  (timestamp - before.timestamp) / (after.timestamp - before.timestamp), 0.0, 1.0))
                            : 0.0f;
    std::unordered_map<Entity, const ReplicatedEntityState*> previous;
    for (const auto& state : before.entities)
        previous[state.entity] = &state;
    states = after.entities;
    for (auto& state : states)
        if (const auto found = previous.find(state.entity); found != previous.end())
        {
            const auto& left = *found->second;
            state.position = left.position * (1.0f - alpha) + state.position * alpha;
            state.rotation = left.rotation * (1.0f - alpha) + state.rotation * alpha;
            state.velocity = left.velocity * (1.0f - alpha) + state.velocity * alpha;
        }
    return true;
}

void SnapshotBuffer::clear()
{
    m_snapshots.clear();
}

std::size_t SnapshotBuffer::Size() const
{
    return m_snapshots.size();
}

void ReplayStream::Record(NetworkSnapshot snapshot)
{
    m_buffer.push(snapshot);
    m_frames.push_back(std::move(snapshot));
    std::sort(m_frames.begin(), m_frames.end(),
              [](const NetworkSnapshot& left, const NetworkSnapshot& right)
              { return left.timestamp < right.timestamp; });
}

bool ReplayStream::Save(const std::filesystem::path& path) const
{
    BinaryWriter writer;
    writer.writeBytes("TNYRPLY1", 8);
    writer.writeU32(static_cast<std::uint32_t>(m_frames.size()));
    for (const auto& frame : m_frames)
    {
        const auto bytes = SnapshotCodec::encode(frame);
        writer.writeU32(static_cast<std::uint32_t>(bytes.size()));
        writer.writeBytes(bytes.data(), bytes.size());
    }
    return writer.Save(path);
}

bool ReplayStream::load(const std::filesystem::path& path)
{
    const auto fileBytes = PlatformFile::ReadAllBytes(path);
    if (fileBytes.empty())
        return false;
    BinaryReader reader(fileBytes);
    char magic[8];
    std::uint32_t count = 0;
    if (!reader.readBytes(magic, sizeof(magic)) || std::memcmp(magic, "TNYRPLY1", 8) != 0 || !reader.readU32(count) ||
        count > 1000000)
        return false;
    clear();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t size = 0;
        if (!reader.readU32(size) || size > 16 * 1024 * 1024)
            return false;
        std::vector<std::uint8_t> bytes(size);
        NetworkSnapshot frame;
        if (!reader.readBytes(bytes.data(), bytes.size()) || !SnapshotCodec::decode(bytes, frame))
            return false;
        Record(std::move(frame));
    }
    return true;
}

bool ReplayStream::sample(const double timestamp, std::vector<ReplicatedEntityState>& states) const
{
    return m_buffer.sample(timestamp, states);
}

void ReplayStream::clear()
{
    m_buffer.clear();
    m_frames.clear();
}

double ReplayStream::getDuration() const
{
    return m_frames.empty() ? 0.0 : m_frames.back().timestamp - m_frames.front().timestamp;
}

namespace
{
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
void closeSocket(const NativeSocket socket)
{
    closesocket(socket);
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
void closeSocket(const NativeSocket socket)
{
    close(socket);
}
#endif

NativeSocket toSocket(const std::uintptr_t value)
{
    return static_cast<NativeSocket>(value);
}

void writeU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > bytes.size())
        return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    return true;
}
} // namespace

UdpSocket::UdpSocket()
{
#ifdef _WIN32
    static const bool kInitialized = []
    {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)kInitialized;
#endif
}

UdpSocket::~UdpSocket()
{
    Close();
}

bool UdpSocket::Open(const std::uint16_t port, const bool nonBlocking)
{
    Close();
    const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        closeSocket(socket);
        return false;
    }
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(socket, FIONBIO, &mode) != 0)
#else
    if (nonBlocking && fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK) != 0)
#endif
    {
        closeSocket(socket);
        return false;
    }
    sockaddr_in bound{};
#ifdef _WIN32
    int boundSize = sizeof(bound);
#else
    socklen_t boundSize = sizeof(bound);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0)
    {
        closeSocket(socket);
        return false;
    }
    m_socket = static_cast<std::uintptr_t>(socket);
    m_boundPort = ntohs(bound.sin_port);
    return true;
}

void UdpSocket::Close()
{
    if (toSocket(m_socket) != kInvalidSocket)
        closeSocket(toSocket(m_socket));
    m_socket = static_cast<std::uintptr_t>(kInvalidSocket);
    m_boundPort = 0;
}

bool UdpSocket::send(const UdpEndpoint& endpoint, const std::vector<std::uint8_t>& data) const
{
    if (!isOpen() || endpoint.port == 0 || data.empty() || data.size() > kNetworkMaximumDatagram)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1)
        return false;
    const int sent =
        sendto(toSocket(m_socket), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0,
               reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    return sent == static_cast<int>(data.size());
}

bool UdpSocket::receive(UdpEndpoint& endpoint, std::vector<std::uint8_t>& data)
{
    if (!isOpen())
        return false;
    std::vector<std::uint8_t> buffer(kNetworkMaximumDatagram);
    sockaddr_in address{};
#ifdef _WIN32
    int addressSize = sizeof(address);
#else
    socklen_t addressSize = sizeof(address);
#endif
    const int received =
        recvfrom(toSocket(m_socket), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                 reinterpret_cast<sockaddr*>(&address), &addressSize);
    if (received <= 0)
        return false;
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)))
        return false;
    endpoint = {text, ntohs(address.sin_port)};
    data.assign(buffer.begin(), buffer.begin() + received);
    return true;
}

std::uint16_t UdpSocket::getBoundPort() const
{
    return m_boundPort;
}

bool UdpSocket::isOpen() const
{
    return toSocket(m_socket) != kInvalidSocket;
}

bool ReliableChannel::queue(std::vector<std::uint8_t> payload)
{
    constexpr std::size_t kHeaderSize = 20;
    if (payload.empty() || payload.size() + kHeaderSize > kNetworkMaximumDatagram || m_pending.size() >= 1024)
        return false;
    m_pending.push_back({m_nextSequence++, std::move(payload), -1.0, 0});
    return true;
}

std::vector<std::vector<std::uint8_t>> ReliableChannel::BuildPackets(const double nowSeconds,
                                                                     const double resendSeconds)
{
    std::vector<std::vector<std::uint8_t>> result;
    for (auto& pending : m_pending)
    {
        if (pending.attempts >= 32 ||
            (pending.lastSent >= 0.0 && nowSeconds - pending.lastSent < std::max(0.01, resendSeconds)))
            continue;
        std::vector<std::uint8_t> packet{'T', 'R', 'E', 'L'};
        writeU32(packet, pending.sequence);
        writeU32(packet, m_remoteSequence);
        writeU32(packet, m_receivedBits);
        writeU32(packet, static_cast<std::uint32_t>(pending.payload.size()));
        packet.insert(packet.end(), pending.payload.begin(), pending.payload.end());
        pending.lastSent = nowSeconds;
        ++pending.attempts;
        result.push_back(std::move(packet));
    }
    return result;
}

// ackBits 的第 n 位确认 ack-(n+1)，使用无符号环绕比较处理序列号溢出。
void ReliableChannel::applyAcks(const std::uint32_t ack, const std::uint32_t ackBits)
{
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                   [ack, ackBits](const Pending& pending)
                                   {
                                       if (pending.sequence == ack)
                                           return true;
                                       const std::uint32_t distance = ack - pending.sequence;
                                       return distance >= 1 && distance <= 32 &&
                                              (ackBits & (1u << (distance - 1u))) != 0;
                                   }),
                    m_pending.end());
}

bool ReliableChannel::processPacket(const std::vector<std::uint8_t>& packet, const double,
                                    std::vector<std::uint8_t>& payload)
{
    if (packet.size() < 20 || packet.size() > kNetworkMaximumDatagram ||
        !std::equal(packet.begin(), packet.begin() + 4, "TREL"))
        return false;
    std::size_t offset = 4;
    std::uint32_t sequence = 0, ack = 0, ackBits = 0, payloadSize = 0;
    if (!readU32(packet, offset, sequence) || !readU32(packet, offset, ack) || !readU32(packet, offset, ackBits) ||
        !readU32(packet, offset, payloadSize) || payloadSize != packet.size() - offset)
        return false;
    applyAcks(ack, ackBits);
    bool duplicate = false;
    if (sequence > m_remoteSequence)
    {
        const std::uint32_t distance = sequence - m_remoteSequence;
        m_receivedBits = distance > 32 ? 0 : (m_receivedBits << distance) | (1u << (distance - 1u));
        m_remoteSequence = sequence;
    }
    else
    {
        const std::uint32_t distance = m_remoteSequence - sequence;
        if (distance == 0 || distance > 32)
            duplicate = true;
        else
        {
            const std::uint32_t bit = 1u << (distance - 1u);
            duplicate = (m_receivedBits & bit) != 0;
            m_receivedBits |= bit;
        }
    }
    if (duplicate)
        return false;
    payload.assign(packet.begin() + static_cast<std::ptrdiff_t>(offset), packet.end());
    return true;
}

std::size_t ReliableChannel::pendingCount() const
{
    return m_pending.size();
}

std::uint32_t ReliableChannel::getLastReceivedSequence() const
{
    return m_remoteSequence;
}

NetworkRateLimiter::NetworkRateLimiter(const double ratePerSecond, const double burst)
    : m_rate(std::max(0.0, ratePerSecond)), m_capacity(std::max(1.0, burst)), m_tokens(m_capacity)
{
}

bool NetworkRateLimiter::allow(const double nowSeconds, const double cost)
{
    if (!std::isfinite(nowSeconds) || cost <= 0.0 || cost > m_capacity)
        return false;
    if (m_lastTime >= 0.0)
        m_tokens = std::min(m_capacity, m_tokens + std::max(0.0, nowSeconds - m_lastTime) * m_rate);
    m_lastTime = nowSeconds;
    if (m_tokens < cost)
        return false;
    m_tokens -= cost;
    return true;
}

bool RpcRegistry::Register(const std::uint16_t id, Handler handler)
{
    return id != 0 && handler && m_handlers.emplace(id, std::move(handler)).second;
}

std::vector<std::uint8_t> RpcRegistry::encode(const std::uint16_t id, const std::vector<std::uint8_t>& payload) const
{
    if (!m_handlers.count(id) || payload.size() > kNetworkMaximumRpcPayload)
        return {};
    std::vector<std::uint8_t> result{'T', 'R', 'P', 'C'};
    writeU32(result, id);
    writeU32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

bool RpcRegistry::dispatch(const std::uint32_t clientId, const std::vector<std::uint8_t>& packet,
                           const double nowSeconds)
{
    if (packet.size() < 12 || packet.size() > kNetworkMaximumRpcPayload + 12 ||
        !std::equal(packet.begin(), packet.begin() + 4, "TRPC") || !m_limiters[clientId].allow(nowSeconds))
        return false;
    std::size_t offset = 4;
    std::uint32_t id = 0, size = 0;
    if (!readU32(packet, offset, id) || !readU32(packet, offset, size) ||
        id > std::numeric_limits<std::uint16_t>::max() || size != packet.size() - offset ||
        size > kNetworkMaximumRpcPayload)
        return false;
    const auto found = m_handlers.find(static_cast<std::uint16_t>(id));
    if (found == m_handlers.end())
        return false;
    found->second(clientId,
                  std::vector<std::uint8_t>(packet.begin() + static_cast<std::ptrdiff_t>(offset), packet.end()));
    return true;
}

bool NetworkOwnership::assign(const Entity entity, const std::uint32_t clientId)
{
    if (entity == kInvalidEntity || clientId == 0)
        return false;
    const auto found = m_owners.find(entity);
    if (found != m_owners.end() && found->second != clientId)
        return false;
    m_owners[entity] = clientId;
    return true;
}

bool NetworkOwnership::release(const Entity entity, const std::uint32_t clientId)
{
    const auto found = m_owners.find(entity);
    if (found == m_owners.end() || found->second != clientId)
        return false;
    m_owners.erase(found);
    return true;
}

bool NetworkOwnership::isOwner(const Entity entity, const std::uint32_t clientId) const
{
    const auto found = m_owners.find(entity);
    return found != m_owners.end() && found->second == clientId;
}

void NetworkOwnership::removeClient(const std::uint32_t clientId)
{
    for (auto iterator = m_owners.begin(); iterator != m_owners.end();)
        if (iterator->second == clientId)
            iterator = m_owners.erase(iterator);
        else
            ++iterator;
}

void ClientPrediction::reset(const Vec3& position)
{
    m_position = position;
    m_inputs.clear();
}

Vec3 ClientPrediction::applyInput(const PredictedInput& input)
{
    if (input.sequence == 0 || input.deltaTime < 0.0f || input.deltaTime > 0.25f || !std::isfinite(input.deltaTime))
        return m_position;
    if (!m_inputs.empty() && input.sequence <= m_inputs.back().sequence)
        return m_position;
    if (m_inputs.size() >= 256)
        m_inputs.pop_front();
    m_inputs.push_back(input);
    m_position += input.movement * input.deltaTime;
    return m_position;
}

// 丢弃服务器已确认输入，从权威位置重放余下输入；误差过大时直接吸附以阻止长期发散。
Vec3 ClientPrediction::reconcile(const std::uint32_t acknowledgedInput, const Vec3& authoritativePosition,
                                 const float snapDistance)
{
    while (!m_inputs.empty() && m_inputs.front().sequence <= acknowledgedInput)
        m_inputs.pop_front();
    Vec3 replayed = authoritativePosition;
    for (const auto& input : m_inputs)
        replayed += input.movement * input.deltaTime;
    const float error = length(replayed - m_position);
    m_position = error > std::max(0.0f, snapDistance) ? replayed : m_position + (replayed - m_position) * 0.5f;
    return m_position;
}

const Vec3& ClientPrediction::GetPosition() const
{
    return m_position;
}

std::size_t ClientPrediction::pendingInputCount() const
{
    return m_inputs.size();
}
} // namespace tinyengine
