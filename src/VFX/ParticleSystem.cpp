#include "MiniEngine/VFX/ParticleSystem.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace MiniEngine
{
ParticleEmitter::ParticleEmitter(ParticleEmitterSettings settings) : m_settings(std::move(settings))
{
    m_particles.reserve(m_settings.maximumParticles);
}

void ParticleEmitter::SetPosition(const Vec3 &position)
{
    m_position = position;
}

void ParticleEmitter::SetPlaying(const bool playing)
{
    m_playing = playing;
}

void ParticleEmitter::Burst(const std::size_t count)
{
    if (m_settings.simulationMode == ParticleSimulationMode::Cpu)
        Spawn(count);
}

void ParticleEmitter::Update(const float deltaTime)
{
    if (deltaTime <= 0.0f)
        return;
    m_elapsedTime += deltaTime;
    if (m_settings.simulationMode == ParticleSimulationMode::Gpu)
        return;
    if (m_playing && m_settings.emissionRate > 0.0f)
    {
        m_emissionAccumulator += deltaTime * m_settings.emissionRate;
        const auto count = static_cast<std::size_t>(m_emissionAccumulator);
        m_emissionAccumulator -= static_cast<float>(count);
        Spawn(count);
    }
    for (auto &particle : m_particles)
    {
        particle.age += deltaTime;
        particle.velocity += m_settings.gravity * deltaTime;
        particle.position += particle.velocity * deltaTime;
        const float alpha = std::clamp(particle.age / std::max(0.001f, particle.lifetime), 0.0f, 1.0f);
        particle.size = m_settings.startSize * (1.0f - alpha) + m_settings.endSize * alpha;
        particle.color = {m_settings.startColor.x * (1.0f - alpha) + m_settings.endColor.x * alpha,
                          m_settings.startColor.y * (1.0f - alpha) + m_settings.endColor.y * alpha,
                          m_settings.startColor.z * (1.0f - alpha) + m_settings.endColor.z * alpha,
                          m_settings.startColor.w * (1.0f - alpha) + m_settings.endColor.w * alpha};
        const std::uint32_t frameCount = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(m_settings.flipbookColumns) *
                                                                        static_cast<std::uint32_t>(m_settings.flipbookRows));
        std::uint32_t frame = static_cast<std::uint32_t>(particle.age * std::max(0.0f, m_settings.flipbookFramesPerSecond));
        frame = m_settings.flipbookLooping ? frame % frameCount : std::min(frame, frameCount - 1);
        particle.flipbookFrame = static_cast<std::uint16_t>(frame);
    }
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle &particle) { return particle.age >= particle.lifetime; }),
        m_particles.end());
}

const std::vector<Particle> &ParticleEmitter::GetParticles() const
{
    return m_particles;
}

const ParticleEmitterSettings &ParticleEmitter::GetSettings() const
{
    return m_settings;
}

const Vec3 &ParticleEmitter::GetPosition() const
{
    return m_position;
}

float ParticleEmitter::GetElapsedTime() const
{
    return m_elapsedTime;
}

float ParticleEmitter::RandomSigned()
{
    m_randomState = m_randomState * 1664525u + 1013904223u;
    return static_cast<float>((m_randomState >> 8u) & 0xffffu) / 32767.5f - 1.0f;
}

void ParticleEmitter::Spawn(const std::size_t count)
{
    const std::size_t available = m_settings.maximumParticles > m_particles.size() ? m_settings.maximumParticles - m_particles.size() : 0;
    for (std::size_t index = 0; index < std::min(count, available); ++index)
    {
        Vec3 direction{RandomSigned() * m_settings.spread, 1.0f, RandomSigned() * m_settings.spread};
        direction = Normalize(direction);
        Particle particle;
        particle.position = m_position;
        particle.velocity = direction * m_settings.speed;
        particle.color = m_settings.startColor;
        particle.lifetime = std::max(0.001f, m_settings.lifetime);
        particle.size = m_settings.startSize;
        particle.flipbookColumns = std::max<std::uint16_t>(1, m_settings.flipbookColumns);
        particle.flipbookRows = std::max<std::uint16_t>(1, m_settings.flipbookRows);
        particle.texturePath = m_settings.texturePath;
        m_particles.push_back(particle);
    }
}

std::uint64_t VfxSystem::CreateEmitter(const ParticleEmitterSettings &settings)
{
    const std::uint64_t id = m_nextId++;
    m_emitters.emplace(id, ParticleEmitter{settings});
    return id;
}

ParticleEmitter *VfxSystem::FindEmitter(const std::uint64_t id)
{
    const auto found = m_emitters.find(id);
    return found == m_emitters.end() ? nullptr : &found->second;
}

bool VfxSystem::DestroyEmitter(const std::uint64_t id)
{
    return m_emitters.erase(id) != 0;
}

void VfxSystem::Update(const float deltaTime)
{
    for (auto &pair : m_emitters)
        pair.second.Update(deltaTime);
}

void VfxSystem::Clear()
{
    m_emitters.clear();
}

std::vector<Particle> VfxSystem::BuildParticleList() const
{
    std::vector<Particle> result;
    for (const auto &pair : m_emitters)
        result.insert(result.end(), pair.second.GetParticles().begin(), pair.second.GetParticles().end());
    return result;
}

std::vector<ParticleBatch> VfxSystem::BuildBatches() const
{
    std::vector<ParticleBatch> result;
    for (const auto &pair : m_emitters)
    {
        const auto &settings = pair.second.GetSettings();
        auto found = std::find_if(result.begin(), result.end(), [&settings](const ParticleBatch &batch) {
            return batch.texturePath == settings.texturePath && batch.simulationMode == settings.simulationMode;
        });
        if (found == result.end())
        {
            result.push_back({settings.texturePath, settings.simulationMode, {}});
            found = result.end() - 1;
        }
        for (const auto &particle : pair.second.GetParticles())
        {
            const float columns = static_cast<float>(std::max<std::uint16_t>(1, particle.flipbookColumns));
            const float rows = static_cast<float>(std::max<std::uint16_t>(1, particle.flipbookRows));
            const float column = static_cast<float>(particle.flipbookFrame % static_cast<std::uint16_t>(columns));
            const float row = static_cast<float>(particle.flipbookFrame / static_cast<std::uint16_t>(columns));
            found->instances.push_back({particle.position,
                                        particle.size,
                                        particle.color,
                                        {column / columns, row / rows, (column + 1.0f) / columns, (row + 1.0f) / rows}});
        }
    }
    return result;
}

std::vector<GpuParticleEmitter> VfxSystem::BuildGpuEmitterList() const
{
    std::vector<GpuParticleEmitter> result;
    for (const auto &pair : m_emitters)
        if (pair.second.GetSettings().simulationMode == ParticleSimulationMode::Gpu)
            result.push_back({pair.first, pair.second.GetSettings(), pair.second.GetPosition(), pair.second.GetElapsedTime()});
    return result;
}

bool SaveParticleEffectAsset(const ParticleEmitterSettings &settings, const std::filesystem::path &path)
{
    if (settings.maximumParticles == 0 || settings.lifetime <= 0.0f || settings.flipbookColumns == 0 || settings.flipbookRows == 0)
        return false;
    std::ostringstream output;
    output << std::setprecision(9) << "TINY_VFX 1\n";
    output << "capacity " << settings.maximumParticles << "\nemission " << settings.emissionRate << "\nlifetime " << settings.lifetime
           << "\nspeed " << settings.speed << "\nspread " << settings.spread << "\nsize " << settings.startSize << ' ' << settings.endSize
           << "\ngravity " << settings.gravity.x << ' ' << settings.gravity.y << ' ' << settings.gravity.z << "\nstartcolor "
           << settings.startColor.x << ' ' << settings.startColor.y << ' ' << settings.startColor.z << ' ' << settings.startColor.w
           << "\nendcolor " << settings.endColor.x << ' ' << settings.endColor.y << ' ' << settings.endColor.z << ' ' << settings.endColor.w
           << "\ntexture " << std::quoted(settings.texturePath) << "\nflipbook " << settings.flipbookColumns << ' ' << settings.flipbookRows
           << ' ' << settings.flipbookFramesPerSecond << ' ' << settings.flipbookLooping << "\nmode "
           << (settings.simulationMode == ParticleSimulationMode::Gpu ? "gpu" : "cpu") << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}

bool LoadParticleEffectAsset(const std::filesystem::path &path, ParticleEmitterSettings &settings)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string magic, token, mode;
    int version = 0;
    ParticleEmitterSettings result;
    if (!(input >> magic >> version) || magic != "TINY_VFX" || version != 1)
        return false;
    while (input >> token)
    {
        if (token == "capacity")
            input >> result.maximumParticles;
        else if (token == "emission")
            input >> result.emissionRate;
        else if (token == "lifetime")
            input >> result.lifetime;
        else if (token == "speed")
            input >> result.speed;
        else if (token == "spread")
            input >> result.spread;
        else if (token == "size")
            input >> result.startSize >> result.endSize;
        else if (token == "gravity")
            input >> result.gravity.x >> result.gravity.y >> result.gravity.z;
        else if (token == "startcolor")
            input >> result.startColor.x >> result.startColor.y >> result.startColor.z >> result.startColor.w;
        else if (token == "endcolor")
            input >> result.endColor.x >> result.endColor.y >> result.endColor.z >> result.endColor.w;
        else if (token == "texture")
            input >> std::quoted(result.texturePath);
        else if (token == "flipbook")
            input >> result.flipbookColumns >> result.flipbookRows >> result.flipbookFramesPerSecond >> result.flipbookLooping;
        else if (token == "mode")
        {
            input >> mode;
            if (mode != "cpu" && mode != "gpu")
                return false;
            result.simulationMode = mode == "gpu" ? ParticleSimulationMode::Gpu : ParticleSimulationMode::Cpu;
        }
        else
            return false;
        if (!input)
            return false;
    }
    if (result.maximumParticles == 0 || result.lifetime <= 0.0f || result.flipbookColumns == 0 || result.flipbookRows == 0)
        return false;
    settings = std::move(result);
    return true;
}
} // namespace MiniEngine
