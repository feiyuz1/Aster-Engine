#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct Particle
{
    Vec3 position{};
    Vec3 velocity{};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float age = 0.0f;
    float lifetime = 1.0f;
    float size = 1.0f;
};

struct ParticleEmitterSettings
{
    std::size_t maximumParticles = 1024;
    float emissionRate = 20.0f;
    float lifetime = 2.0f;
    float speed = 2.0f;
    float spread = 0.5f;
    float startSize = 0.2f;
    float endSize = 0.0f;
    Vec3 gravity{0.0f, -9.81f, 0.0f};
    Vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
};

class ParticleEmitter
{
  public:
    explicit ParticleEmitter(ParticleEmitterSettings settings = {});
    void SetPosition(const Vec3 &position);
    void SetPlaying(bool playing);
    void Burst(std::size_t count);
    void Update(float deltaTime);
    const std::vector<Particle> &GetParticles() const;
    const ParticleEmitterSettings &GetSettings() const;

  private:
    float RandomSigned();
    void Spawn(std::size_t count);
    ParticleEmitterSettings m_settings;
    std::vector<Particle> m_particles;
    Vec3 m_position{};
    float m_emissionAccumulator = 0.0f;
    bool m_playing = true;
    std::uint32_t m_randomState = 0x12345678u;
};

class VfxSystem
{
  public:
    std::uint64_t CreateEmitter(const ParticleEmitterSettings &settings = {});
    ParticleEmitter *FindEmitter(std::uint64_t id);
    bool DestroyEmitter(std::uint64_t id);
    void Update(float deltaTime);
    void Clear();

  private:
    std::unordered_map<std::uint64_t, ParticleEmitter> m_emitters;
    std::uint64_t m_nextId = 1;
};
} // namespace MiniEngine
