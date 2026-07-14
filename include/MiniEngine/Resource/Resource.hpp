#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace MiniEngine
{
enum class ResourceState
{
    Unloaded,
    Loading,
    Loaded,
    Failed,
    PendingUnload
};

class Resource
{
  public:
    virtual ~Resource() = default;
    virtual bool Load(const std::string &path) = 0;
    virtual void Unload() = 0;
    virtual std::size_t GetByteSize() const = 0;

    const std::string &GetPath() const;
    ResourceState GetState() const;
    std::string GetError() const;
    bool IsReady() const;
    bool Wait() const;
    void MarkLoading(const std::string &path = {});

  protected:
    void SetState(ResourceState state, std::string error = {});
    std::string m_path;

  private:
    std::atomic<ResourceState> m_state{ResourceState::Unloaded};
    mutable std::mutex m_stateMutex;
    mutable std::condition_variable m_stateCv;
    std::string m_error;
};

class TextureResource final : public Resource
{
  public:
    bool Load(const std::string &path) override;
    void Unload() override;
    std::size_t GetByteSize() const override;
    const std::vector<std::uint8_t> &GetData() const;
    const std::vector<float> &GetFloatData() const;
    bool IsHdr() const;
    int GetWidth() const;
    int GetHeight() const;

  private:
    std::vector<std::uint8_t> m_data;
    std::vector<float> m_floatData;
    int m_width = 0;
    int m_height = 0;
};

struct MeshVertex
{
    Vec3 position;
    Vec3 normal{0.0f, 0.0f, 1.0f};
    Vec2 uv;
    Vec3 tangent{1.0f, 0.0f, 0.0f};
};

class MeshResource final : public Resource
{
  public:
    bool Load(const std::string &path) override;
    void Unload() override;
    std::size_t GetByteSize() const override;
    const std::vector<MeshVertex> &GetVertices() const;
    const std::vector<std::uint32_t> &GetIndices() const;
    const Aabb &GetBounds() const;

  private:
    std::vector<MeshVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    Aabb m_bounds{};
};

class ShaderResource final : public Resource
{
  public:
    bool Load(const std::string &path) override;
    void Unload() override;
    std::size_t GetByteSize() const override;
    const std::string &GetSource() const;

  private:
    std::string m_source;
};

class MaterialResource final : public Resource
{
  public:
    bool Load(const std::string &path) override;
    void Unload() override;
    std::size_t GetByteSize() const override;
    Vec4 GetBaseColor() const;
    const std::string &GetShaderPath() const;
    const std::string &GetTexturePath() const;
    const std::string &GetNormalTexturePath() const;
    const std::string &GetMetallicTexturePath() const;
    const std::string &GetRoughnessTexturePath() const;
    const std::string &GetAoTexturePath() const;
    const std::string &GetEmissiveTexturePath() const;
    float GetMetallic() const;
    float GetRoughness() const;
    Vec3 GetEmissive() const;
    bool IsTransparent() const;

  private:
    Vec4 m_baseColor{0.3f, 0.7f, 1.0f, 1.0f};
    std::string m_shaderPath;
    std::string m_texturePath;
    std::string m_normalTexturePath;
    std::string m_metallicTexturePath;
    std::string m_roughnessTexturePath;
    std::string m_aoTexturePath;
    std::string m_emissiveTexturePath;
    float m_metallic = 0.0f;
    float m_roughness = 0.7f;
    Vec3 m_emissive{};
    bool m_transparent = false;
};
} // namespace MiniEngine
