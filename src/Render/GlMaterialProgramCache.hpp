#pragma once
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include "MiniEngine/Render/Renderer.hpp"
#include <functional>
#include <memory>

namespace MiniEngine
{
struct GlMaterialProgram
{
    std::uint32_t handle = 0;
    std::unordered_map<std::string,int> locations;
    MaterialPass pass = MaterialPass::Base;
    MaterialGeometry geometry = MaterialGeometry::Static;
};
class GlMaterialProgramCache
{
public:
    using TextureUploader = std::function<std::uint32_t(const std::shared_ptr<TextureResource> &,bool,std::uint64_t)>;
    explicit GlMaterialProgramCache(TextureUploader uploader);
    ~GlMaterialProgramCache();
    bool Initialize();
    void SetTemplates(MaterialTemplateSources sources);
    void SetFrameBindings(const std::array<std::uint32_t,3> &shadows, std::uint32_t lightData, std::uint32_t clusterData,
                          std::size_t cascade, Vec2 viewport);
    GlMaterialProgram *GetOrCreate(const CompiledMaterial &,MaterialPass,MaterialGeometry);
    bool Bind(const MaterialRenderProxy &,const FrameView &,MaterialPass,MaterialGeometry);
    void BindObject(const RenderItem &,const FrameView &,MaterialGeometry);
    std::size_t GetCompileCount(MaterialPass) const;
    const std::vector<MaterialDiagnostic> &GetDiagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace MiniEngine
