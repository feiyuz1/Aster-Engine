// 文件：include/MiniEngine/Resource/Resource.hpp
// 作用：定义资源状态机以及纹理、网格、材质和着色器的运行时数据。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Render/MaterialTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
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

// 资源状态机的同步基类。加载线程通过 SetState 唤醒等待者；Failed 状态同时保存错误文本。
class Resource
{
  public:
    /// @brief 析构 `Resource` 对象并释放其持有资源。
    virtual ~Resource() = default;
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool Load(const std::string &path) = 0;
    /// @brief 卸载资源并释放相关缓存。
    virtual void Unload() = 0;
    /// @brief 获取 `ByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    virtual std::size_t GetByteSize() const = 0;

    /// @brief 获取 `Path`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetPath() const;
    /// @brief 获取 `State`。
    /// @return 返回接口计算或查询得到的结果。
    ResourceState GetState() const;
    /// @brief 获取 `Error`。
    /// @return 返回对应的文本或路径。
    std::string GetError() const;
    /// @brief 判断是否满足 `Ready` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsReady() const;
    /// @brief 等待操作完成，并在失败时传播错误。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Wait() const;
    /// @brief 标记 `Loading`。
    /// @param path 文件、资源或目录路径。
    void MarkLoading(const std::string &path = {});

  protected:
    /// @brief 设置 `State`。
    /// @param state 目标状态、模式或类型。
    /// @param error 可选的错误信息输出；可为空。
    void SetState(ResourceState state, std::string error = {});
    std::string m_path;

  private:
    std::atomic<ResourceState> m_state{ResourceState::Unloaded};
    mutable std::mutex m_stateMutex;
    mutable std::condition_variable m_stateCv;
    std::string m_error;
};

// CPU 侧纹理载荷；压缩块和 Mip 只记录视图，真正的 GPU 上传由 RenderDevice 完成。
class TextureResource final : public Resource
{
  public:
    enum class Compression
    {
        None,
        BC1,
        BC2,
        BC3,
        BC4,
        BC5,
        BC6H,
        BC7
    };
    struct MipLevel
    {
        int width = 0;
        int height = 0;
        std::size_t offset = 0;
        std::size_t size = 0;
    };
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::string &path) override;
    /// @brief 卸载资源并释放相关缓存。
    void Unload() override;
    /// @brief 获取 `ByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetByteSize() const override;
    /// @brief 获取 `Data`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<std::uint8_t> &GetData() const;
    /// @brief 获取 `FloatData`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<float> &GetFloatData() const;
    /// @brief 判断是否满足 `Hdr` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsHdr() const;
    /// @brief 获取 `Width`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetWidth() const;
    /// @brief 获取 `Height`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetHeight() const;
    /// @brief 获取 `Compression`。
    /// @return 返回接口计算或查询得到的结果。
    Compression GetCompression() const;
    /// @brief 获取 `MipLevels`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<MipLevel> &GetMipLevels() const;
    /// @brief 判断是否满足 `SrgbEncoded` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsSrgbEncoded() const;

  private:
    std::vector<std::uint8_t> m_data;
    std::vector<float> m_floatData;
    int m_width = 0;
    int m_height = 0;
    Compression m_compression = Compression::None;
    std::vector<MipLevel> m_mipLevels;
    bool m_srgbEncoded = false;
};

// 固定顶点布局与默认着色器一致；骨骼权重最多四个且导入后已归一化。
struct MeshVertex
{
    Vec3 position;
    Vec3 normal{0.0f, 0.0f, 1.0f};
    Vec2 uv;
    Vec3 tangent{1.0f, 0.0f, 0.0f};
    Vec4 boneIndices{};
    Vec4 boneWeights{};
};

class MeshResource final : public Resource
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::string &path) override;
    /// @brief 卸载资源并释放相关缓存。
    void Unload() override;
    /// @brief 获取 `ByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetByteSize() const override;
    /// @brief 获取 `Vertices`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<MeshVertex> &GetVertices() const;
    /// @brief 获取 `Indices`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<std::uint32_t> &GetIndices() const;
    /// @brief 获取 `Bounds`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const Aabb &GetBounds() const;

  private:
    std::vector<MeshVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
    Aabb m_bounds{};
};

class ShaderResource final : public Resource
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::string &path) override;
    /// @brief 卸载资源并释放相关缓存。
    void Unload() override;
    /// @brief 获取 `ByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetByteSize() const override;
    /// @brief 获取 `Source`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetSource() const;

  private:
    std::string m_source;
};

// 材质实例保存父材质路径和显式覆盖位；未覆盖属性应在解析时继承父级值。
class MaterialResource final : public Resource
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::string &path) override;
    bool Save(const std::string &path) const;
    const MaterialAssetData &GetMaterialData() const { return m_materialData; }
    using ReferenceResolver = std::function<std::string(const std::string &, const std::string &)>;
    using SourceReader=std::function<std::string(const std::string &)>;
    bool LoadWithResolver(const std::string &path, ReferenceResolver resolver, std::size_t maxParentDepth = 32, SourceReader reader = {});
    /// @brief 卸载资源并释放相关缓存。
    void Unload() override;
    /// @brief 获取 `ByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetByteSize() const override;
    /// @brief 获取 `BaseColor`。
    /// @return 返回接口计算或查询得到的结果。
    Vec4 GetBaseColor() const;
    /// @brief 获取 `ShaderPath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetShaderPath() const;
    /// @brief 获取 `TexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetTexturePath() const;
    /// @brief 获取 `NormalTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetNormalTexturePath() const;
    /// @brief 获取 `MetallicTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetMetallicTexturePath() const;
    /// @brief 获取 `RoughnessTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetRoughnessTexturePath() const;
    /// @brief 获取 `AoTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetAoTexturePath() const;
    /// @brief 获取 `EmissiveTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetEmissiveTexturePath() const;
    /// @brief 获取 `Metallic`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetMetallic() const;
    /// @brief 获取 `Roughness`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetRoughness() const;
    /// @brief 获取 `Emissive`。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 GetEmissive() const;
    /// @brief 判断是否满足 `Transparent` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsTransparent() const;
    /// @brief 获取 `ParentPath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetParentPath() const;
    /// @brief 判断是否满足 `Overridden` 条件。
    /// @param property 用于查找或标识对象的名称。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsOverridden(const std::string &property) const;
    /// @brief 获取 `Parameters`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::unordered_map<std::string, Vec4> &GetParameters() const;
    /// @brief 获取 `PackedTexturePath`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetPackedTexturePath() const;
    /// @brief 获取 `PackedTextureLayout`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetPackedTextureLayout() const;

  private:
    bool LoadCandidate(const std::string &path);
    void CaptureLegacyData(const std::string &source);
    MaterialAssetData m_materialData;
    ReferenceResolver m_referenceResolver;
    SourceReader m_sourceReader;
    std::size_t m_maxParentDepth = 32;
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
    std::string m_parentPath;
    std::unordered_map<std::string, bool> m_overrides;
    std::unordered_map<std::string, Vec4> m_parameters;
    std::string m_packedTexturePath;
    std::string m_packedTextureLayout = "ORM";
};
} // 结束 MiniEngine 命名空间
