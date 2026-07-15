#include "MiniEngine/Network/Replication.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

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
} // namespace MiniEngine
