// 文件：include/MiniEngine/VFX/ParticleSystem.hpp
// 作用：定义 CPU/GPU 粒子发射器、生命周期仿真和渲染数据。
// 约定：粒子数量受预算约束，渲染数据每帧重建且不跨帧持有指针。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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
    std::uint16_t flipbookFrame = 0;
    std::uint16_t flipbookColumns = 1;
    std::uint16_t flipbookRows = 1;
    std::string texturePath;
};

enum class ParticleSimulationMode
{
    Cpu,
    Gpu
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
    std::string texturePath;
    std::uint16_t flipbookColumns = 1;
    std::uint16_t flipbookRows = 1;
    float flipbookFramesPerSecond = 0.0f;
    bool flipbookLooping = true;
    ParticleSimulationMode simulationMode = ParticleSimulationMode::Cpu;
};

struct ParticleInstance
{
    Vec3 position{};
    float size = 1.0f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
};

struct ParticleBatch
{
    std::string texturePath;
    ParticleSimulationMode simulationMode = ParticleSimulationMode::Cpu;
    std::vector<ParticleInstance> instances;
};

struct GpuParticleEmitter
{
    std::uint64_t id = 0;
    ParticleEmitterSettings settings;
    Vec3 position{};
    float elapsedTime = 0.0f;
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
    const Vec3 &GetPosition() const;
    float GetElapsedTime() const;

  private:
    float RandomSigned();
    void Spawn(std::size_t count);
    ParticleEmitterSettings m_settings;
    std::vector<Particle> m_particles;
    Vec3 m_position{};
    float m_emissionAccumulator = 0.0f;
    bool m_playing = true;
    std::uint32_t m_randomState = 0x12345678u;
    float m_elapsedTime = 0.0f;
};

class VfxSystem
{
  public:
    std::uint64_t CreateEmitter(const ParticleEmitterSettings &settings = {});
    ParticleEmitter *FindEmitter(std::uint64_t id);
    bool DestroyEmitter(std::uint64_t id);
    void Update(float deltaTime);
    void Clear();
    std::vector<Particle> BuildParticleList() const;
    std::vector<ParticleBatch> BuildBatches() const;
    std::vector<GpuParticleEmitter> BuildGpuEmitterList() const;

  private:
    std::unordered_map<std::uint64_t, ParticleEmitter> m_emitters;
    std::uint64_t m_nextId = 1;
};

bool SaveParticleEffectAsset(const ParticleEmitterSettings &settings, const std::filesystem::path &path);
bool LoadParticleEffectAsset(const std::filesystem::path &path, ParticleEmitterSettings &settings);
} // namespace MiniEngine
