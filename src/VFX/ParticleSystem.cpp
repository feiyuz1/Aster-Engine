#include "MiniEngine/VFX/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>

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
    Spawn(count);
}

void ParticleEmitter::Update(const float deltaTime)
{
    if (deltaTime <= 0.0f)
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
        m_particles.push_back({m_position, direction * m_settings.speed, m_settings.startColor, 0.0f, std::max(0.001f, m_settings.lifetime),
                               m_settings.startSize});
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
} // namespace MiniEngine
