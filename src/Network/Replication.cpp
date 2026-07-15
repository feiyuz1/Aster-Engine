#include "MiniEngine/Network/Replication.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

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

namespace MiniEngine
{
namespace
{
void WriteState(BinaryWriter &writer, const ReplicatedEntityState &state)
{
    writer.WriteU64(state.entity);
    for (const Vec3 value : {state.position, state.rotation, state.velocity})
    {
        writer.WriteFloat(value.x);
        writer.WriteFloat(value.y);
        writer.WriteFloat(value.z);
    }
}

bool ReadState(BinaryReader &reader, ReplicatedEntityState &state)
{
    return reader.ReadU64(state.entity) && reader.ReadFloat(state.position.x) && reader.ReadFloat(state.position.y) &&
           reader.ReadFloat(state.position.z) && reader.ReadFloat(state.rotation.x) && reader.ReadFloat(state.rotation.y) &&
           reader.ReadFloat(state.rotation.z) && reader.ReadFloat(state.velocity.x) && reader.ReadFloat(state.velocity.y) &&
           reader.ReadFloat(state.velocity.z);
}
} // namespace

std::vector<std::uint8_t> SnapshotCodec::Encode(const NetworkSnapshot &snapshot)
{
    BinaryWriter writer;
    writer.WriteBytes("TNYNET1\0", 8);
    writer.WriteU32(snapshot.sequence);
    std::uint64_t timestampBits = 0;
    static_assert(sizeof(timestampBits) == sizeof(snapshot.timestamp));
    std::memcpy(&timestampBits, &snapshot.timestamp, sizeof(timestampBits));
    writer.WriteU64(timestampBits);
    writer.WriteU32(static_cast<std::uint32_t>(std::min<std::size_t>(snapshot.entities.size(), 65536)));
    for (std::size_t index = 0; index < snapshot.entities.size() && index < 65536; ++index)
        WriteState(writer, snapshot.entities[index]);
    return writer.GetData();
}

bool SnapshotCodec::Decode(const std::vector<std::uint8_t> &bytes, NetworkSnapshot &snapshot)
{
    if (bytes.size() < 24 || !std::equal(bytes.begin(), bytes.begin() + 8, "TNYNET1\0"))
        return false;
    BinaryReader reader(bytes);
    char magic[8];
    std::uint64_t timestampBits = 0;
    std::uint32_t count = 0;
    if (!reader.ReadBytes(magic, sizeof(magic)) || !reader.ReadU32(snapshot.sequence) || !reader.ReadU64(timestampBits) ||
        !reader.ReadU32(count) || count > 65536)
        return false;
    std::memcpy(&snapshot.timestamp, &timestampBits, sizeof(timestampBits));
    snapshot.entities.assign(count, {});
    for (auto &state : snapshot.entities)
        if (!ReadState(reader, state))
            return false;
    return true;
}

void SnapshotBuffer::Push(NetworkSnapshot snapshot)
{
    const auto existing = std::find_if(m_snapshots.begin(), m_snapshots.end(),
                                       [&](const NetworkSnapshot &value) { return value.sequence == snapshot.sequence; });
    if (existing != m_snapshots.end())
        *existing = std::move(snapshot);
    else
        m_snapshots.push_back(std::move(snapshot));
    std::sort(m_snapshots.begin(), m_snapshots.end(),
              [](const NetworkSnapshot &left, const NetworkSnapshot &right) { return left.timestamp < right.timestamp; });
    if (m_snapshots.size() > 64)
        m_snapshots.erase(m_snapshots.begin(), m_snapshots.end() - 64);
}

bool SnapshotBuffer::Sample(const double timestamp, std::vector<ReplicatedEntityState> &states) const
{
    if (m_snapshots.empty())
        return false;
    const auto right = std::lower_bound(m_snapshots.begin(), m_snapshots.end(), timestamp,
                                        [](const NetworkSnapshot &snapshot, const double time) { return snapshot.timestamp < time; });
    if (right == m_snapshots.begin() || right == m_snapshots.end())
    {
        states = (right == m_snapshots.begin() ? m_snapshots.front() : m_snapshots.back()).entities;
        return true;
    }
    const auto &before = *(right - 1), &after = *right;
    const float alpha =
        after.timestamp > before.timestamp
            ? static_cast<float>(std::clamp((timestamp - before.timestamp) / (after.timestamp - before.timestamp), 0.0, 1.0))
            : 0.0f;
    std::unordered_map<Entity, const ReplicatedEntityState *> previous;
    for (const auto &state : before.entities)
        previous[state.entity] = &state;
    states = after.entities;
    for (auto &state : states)
        if (const auto found = previous.find(state.entity); found != previous.end())
        {
            const auto &left = *found->second;
            state.position = left.position * (1.0f - alpha) + state.position * alpha;
            state.rotation = left.rotation * (1.0f - alpha) + state.rotation * alpha;
            state.velocity = left.velocity * (1.0f - alpha) + state.velocity * alpha;
        }
    return true;
}

void SnapshotBuffer::Clear()
{
    m_snapshots.clear();
}

std::size_t SnapshotBuffer::Size() const
{
    return m_snapshots.size();
}

void ReplayStream::Record(NetworkSnapshot snapshot)
{
    m_buffer.Push(snapshot);
    m_frames.push_back(std::move(snapshot));
    std::sort(m_frames.begin(), m_frames.end(),
              [](const NetworkSnapshot &left, const NetworkSnapshot &right) { return left.timestamp < right.timestamp; });
}

bool ReplayStream::Save(const std::filesystem::path &path) const
{
    BinaryWriter writer;
    writer.WriteBytes("TNYRPLY1", 8);
    writer.WriteU32(static_cast<std::uint32_t>(m_frames.size()));
    for (const auto &frame : m_frames)
    {
        const auto bytes = SnapshotCodec::Encode(frame);
        writer.WriteU32(static_cast<std::uint32_t>(bytes.size()));
        writer.WriteBytes(bytes.data(), bytes.size());
    }
    return writer.Save(path);
}

bool ReplayStream::Load(const std::filesystem::path &path)
{
    const auto fileBytes = PlatformFile::ReadAllBytes(path);
    if (fileBytes.empty())
        return false;
    BinaryReader reader(fileBytes);
    char magic[8];
    std::uint32_t count = 0;
    if (!reader.ReadBytes(magic, sizeof(magic)) || std::memcmp(magic, "TNYRPLY1", 8) != 0 || !reader.ReadU32(count) || count > 1000000)
        return false;
    Clear();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t size = 0;
        if (!reader.ReadU32(size) || size > 16 * 1024 * 1024)
            return false;
        std::vector<std::uint8_t> bytes(size);
        NetworkSnapshot frame;
        if (!reader.ReadBytes(bytes.data(), bytes.size()) || !SnapshotCodec::Decode(bytes, frame))
            return false;
        Record(std::move(frame));
    }
    return true;
}

bool ReplayStream::Sample(const double timestamp, std::vector<ReplicatedEntityState> &states) const
{
    return m_buffer.Sample(timestamp, states);
}

void ReplayStream::Clear()
{
    m_buffer.Clear();
    m_frames.clear();
}

double ReplayStream::GetDuration() const
{
    return m_frames.empty() ? 0.0 : m_frames.back().timestamp - m_frames.front().timestamp;
}

namespace
{
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
void CloseSocket(const NativeSocket socket)
{
    closesocket(socket);
}
#else
using NativeSocket = int;
constexpr NativeSocket InvalidSocket = -1;
void CloseSocket(const NativeSocket socket)
{
    close(socket);
}
#endif

NativeSocket ToSocket(const std::uintptr_t value)
{
    return static_cast<NativeSocket>(value);
}

void WriteU32(std::vector<std::uint8_t> &bytes, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

bool ReadU32(const std::vector<std::uint8_t> &bytes, std::size_t &offset, std::uint32_t &value)
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
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
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
    if (socket == InvalidSocket)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        CloseSocket(socket);
        return false;
    }
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(socket, FIONBIO, &mode) != 0)
#else
    if (nonBlocking && fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK) != 0)
#endif
    {
        CloseSocket(socket);
        return false;
    }
    sockaddr_in bound{};
#ifdef _WIN32
    int boundSize = sizeof(bound);
#else
    socklen_t boundSize = sizeof(bound);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&bound), &boundSize) != 0)
    {
        CloseSocket(socket);
        return false;
    }
    m_socket = static_cast<std::uintptr_t>(socket);
    m_boundPort = ntohs(bound.sin_port);
    return true;
}

void UdpSocket::Close()
{
    if (ToSocket(m_socket) != InvalidSocket)
        CloseSocket(ToSocket(m_socket));
    m_socket = static_cast<std::uintptr_t>(InvalidSocket);
    m_boundPort = 0;
}

bool UdpSocket::Send(const UdpEndpoint &endpoint, const std::vector<std::uint8_t> &data) const
{
    if (!IsOpen() || endpoint.port == 0 || data.empty() || data.size() > NetworkMaximumDatagram)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1)
        return false;
    const int sent = sendto(ToSocket(m_socket), reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()), 0,
                            reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    return sent == static_cast<int>(data.size());
}

bool UdpSocket::Receive(UdpEndpoint &endpoint, std::vector<std::uint8_t> &data)
{
    if (!IsOpen())
        return false;
    std::vector<std::uint8_t> buffer(NetworkMaximumDatagram);
    sockaddr_in address{};
#ifdef _WIN32
    int addressSize = sizeof(address);
#else
    socklen_t addressSize = sizeof(address);
#endif
    const int received = recvfrom(ToSocket(m_socket), reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr *>(&address), &addressSize);
    if (received <= 0)
        return false;
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)))
        return false;
    endpoint = {text, ntohs(address.sin_port)};
    data.assign(buffer.begin(), buffer.begin() + received);
    return true;
}

std::uint16_t UdpSocket::GetBoundPort() const
{
    return m_boundPort;
}

bool UdpSocket::IsOpen() const
{
    return ToSocket(m_socket) != InvalidSocket;
}

bool ReliableChannel::Queue(std::vector<std::uint8_t> payload)
{
    constexpr std::size_t headerSize = 20;
    if (payload.empty() || payload.size() + headerSize > NetworkMaximumDatagram || m_pending.size() >= 1024)
        return false;
    m_pending.push_back({m_nextSequence++, std::move(payload), -1.0, 0});
    return true;
}

std::vector<std::vector<std::uint8_t>> ReliableChannel::BuildPackets(const double nowSeconds, const double resendSeconds)
{
    std::vector<std::vector<std::uint8_t>> result;
    for (auto &pending : m_pending)
    {
        if (pending.attempts >= 32 || (pending.lastSent >= 0.0 && nowSeconds - pending.lastSent < std::max(0.01, resendSeconds)))
            continue;
        std::vector<std::uint8_t> packet{'T', 'R', 'E', 'L'};
        WriteU32(packet, pending.sequence);
        WriteU32(packet, m_remoteSequence);
        WriteU32(packet, m_receivedBits);
        WriteU32(packet, static_cast<std::uint32_t>(pending.payload.size()));
        packet.insert(packet.end(), pending.payload.begin(), pending.payload.end());
        pending.lastSent = nowSeconds;
        ++pending.attempts;
        result.push_back(std::move(packet));
    }
    return result;
}

void ReliableChannel::ApplyAcks(const std::uint32_t ack, const std::uint32_t ackBits)
{
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                   [ack, ackBits](const Pending &pending) {
                                       if (pending.sequence == ack)
                                           return true;
                                       const std::uint32_t distance = ack - pending.sequence;
                                       return distance >= 1 && distance <= 32 && (ackBits & (1u << (distance - 1u))) != 0;
                                   }),
                    m_pending.end());
}

bool ReliableChannel::ProcessPacket(const std::vector<std::uint8_t> &packet, const double, std::vector<std::uint8_t> &payload)
{
    if (packet.size() < 20 || packet.size() > NetworkMaximumDatagram || !std::equal(packet.begin(), packet.begin() + 4, "TREL"))
        return false;
    std::size_t offset = 4;
    std::uint32_t sequence = 0, ack = 0, ackBits = 0, payloadSize = 0;
    if (!ReadU32(packet, offset, sequence) || !ReadU32(packet, offset, ack) || !ReadU32(packet, offset, ackBits) ||
        !ReadU32(packet, offset, payloadSize) || payloadSize != packet.size() - offset)
        return false;
    ApplyAcks(ack, ackBits);
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

std::size_t ReliableChannel::PendingCount() const
{
    return m_pending.size();
}

std::uint32_t ReliableChannel::GetLastReceivedSequence() const
{
    return m_remoteSequence;
}

NetworkRateLimiter::NetworkRateLimiter(const double ratePerSecond, const double burst)
    : m_rate(std::max(0.0, ratePerSecond)), m_capacity(std::max(1.0, burst)), m_tokens(m_capacity)
{
}

bool NetworkRateLimiter::Allow(const double nowSeconds, const double cost)
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

std::vector<std::uint8_t> RpcRegistry::Encode(const std::uint16_t id, const std::vector<std::uint8_t> &payload) const
{
    if (!m_handlers.count(id) || payload.size() > NetworkMaximumRpcPayload)
        return {};
    std::vector<std::uint8_t> result{'T', 'R', 'P', 'C'};
    WriteU32(result, id);
    WriteU32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

bool RpcRegistry::Dispatch(const std::uint32_t clientId, const std::vector<std::uint8_t> &packet, const double nowSeconds)
{
    if (packet.size() < 12 || packet.size() > NetworkMaximumRpcPayload + 12 || !std::equal(packet.begin(), packet.begin() + 4, "TRPC") ||
        !m_limiters[clientId].Allow(nowSeconds))
        return false;
    std::size_t offset = 4;
    std::uint32_t id = 0, size = 0;
    if (!ReadU32(packet, offset, id) || !ReadU32(packet, offset, size) || id > std::numeric_limits<std::uint16_t>::max() ||
        size != packet.size() - offset || size > NetworkMaximumRpcPayload)
        return false;
    const auto found = m_handlers.find(static_cast<std::uint16_t>(id));
    if (found == m_handlers.end())
        return false;
    found->second(clientId, std::vector<std::uint8_t>(packet.begin() + static_cast<std::ptrdiff_t>(offset), packet.end()));
    return true;
}

bool NetworkOwnership::Assign(const Entity entity, const std::uint32_t clientId)
{
    if (entity == InvalidEntity || clientId == 0)
        return false;
    const auto found = m_owners.find(entity);
    if (found != m_owners.end() && found->second != clientId)
        return false;
    m_owners[entity] = clientId;
    return true;
}

bool NetworkOwnership::Release(const Entity entity, const std::uint32_t clientId)
{
    const auto found = m_owners.find(entity);
    if (found == m_owners.end() || found->second != clientId)
        return false;
    m_owners.erase(found);
    return true;
}

bool NetworkOwnership::IsOwner(const Entity entity, const std::uint32_t clientId) const
{
    const auto found = m_owners.find(entity);
    return found != m_owners.end() && found->second == clientId;
}

void NetworkOwnership::RemoveClient(const std::uint32_t clientId)
{
    for (auto iterator = m_owners.begin(); iterator != m_owners.end();)
        if (iterator->second == clientId)
            iterator = m_owners.erase(iterator);
        else
            ++iterator;
}

void ClientPrediction::Reset(const Vec3 &position)
{
    m_position = position;
    m_inputs.clear();
}

Vec3 ClientPrediction::ApplyInput(const PredictedInput &input)
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

Vec3 ClientPrediction::Reconcile(const std::uint32_t acknowledgedInput, const Vec3 &authoritativePosition, const float snapDistance)
{
    while (!m_inputs.empty() && m_inputs.front().sequence <= acknowledgedInput)
        m_inputs.pop_front();
    Vec3 replayed = authoritativePosition;
    for (const auto &input : m_inputs)
        replayed += input.movement * input.deltaTime;
    const float error = Length(replayed - m_position);
    m_position = error > std::max(0.0f, snapDistance) ? replayed : m_position + (replayed - m_position) * 0.5f;
    return m_position;
}

const Vec3 &ClientPrediction::GetPosition() const
{
    return m_position;
}

std::size_t ClientPrediction::PendingInputCount() const
{
    return m_inputs.size();
}
} // namespace MiniEngine
