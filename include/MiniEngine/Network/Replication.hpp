#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <cstdint>
#include <filesystem>
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
} // namespace MiniEngine
