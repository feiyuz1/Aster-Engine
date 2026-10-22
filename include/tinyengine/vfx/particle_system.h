// 文件：include/tinyengine/vfx/particle_system.h
// 作用：定义 CPU/GPU 粒子发射器、生命周期仿真和渲染数据。
// 约定：粒子数量受预算约束，渲染数据每帧重建且不跨帧持有指针。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
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
    /// @brief 构造 `ParticleEmitter` 对象。
    /// @param settings 控制系统或操作行为的设置。
    explicit ParticleEmitter(ParticleEmitterSettings settings = {});
    /// @brief 设置 `Position`。
    /// @param position 世界空间或局部空间位置。
    void setPosition(const Vec3& position);
    /// @brief 设置 `Playing`。
    /// @param playing 接口所需的输入参数。
    void setPlaying(bool playing);
    /// @brief 立即生成指定数量的粒子。
    /// @param count 要处理或生成的元素数量。
    void burst(std::size_t count);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 获取 `Particles`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<Particle>& GetParticles() const;
    /// @brief 获取 `Settings`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const ParticleEmitterSettings& GetSettings() const;
    /// @brief 获取 `Position`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const Vec3& GetPosition() const;
    /// @brief 获取 `ElapsedTime`。
    /// @return 返回计算得到的数值、数量或索引。
    float getElapsedTime() const;

  private:
    /// @brief 生成随机 `Signed`。
    /// @return 返回计算得到的数值、数量或索引。
    float randomSigned();
    /// @brief 根据发射器设置生成一个粒子。
    /// @param count 要处理或生成的元素数量。
    void spawn(std::size_t count);
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
    /// @brief 创建 `Emitter`。
    /// @param settings 控制系统或操作行为的设置。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint64_t createEmitter(const ParticleEmitterSettings& settings = {});
    /// @brief 查找 `Emitter`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    ParticleEmitter* findEmitter(std::uint64_t id);
    /// @brief 销毁 `Emitter`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool destroyEmitter(std::uint64_t id);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 清空对象保存的状态和数据。
    void clear();
    /// @brief 构建 `ParticleList`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<Particle> BuildParticleList() const;
    /// @brief 构建 `Batches`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<ParticleBatch> BuildBatches() const;
    /// @brief 构建 `GpuEmitterList`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<GpuParticleEmitter> BuildGpuEmitterList() const;

  private:
    std::unordered_map<std::uint64_t, ParticleEmitter> m_emitters;
    std::uint64_t m_nextId = 1;
};

/// @brief 保存 `ParticleEffectAsset`。
/// @param settings 控制系统或操作行为的设置。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool saveParticleEffectAsset(const ParticleEmitterSettings& settings, const std::filesystem::path& path);
/// @brief 加载 `ParticleEffectAsset`。
/// @param path 文件、资源或目录路径。
/// @param settings 控制系统或操作行为的设置。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool loadParticleEffectAsset(const std::filesystem::path& path, ParticleEmitterSettings& settings);
} // namespace tinyengine
