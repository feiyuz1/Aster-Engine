#pragma once
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/renderer.h"
#include <functional>
#include <memory>

namespace tinyengine
{
struct GlMaterialProgram
{
    std::uint32_t handle = 0;
    std::unordered_map<std::string, int> locations;
    MaterialPass pass = MaterialPass::Base;
    MaterialGeometry geometry = MaterialGeometry::Static;
    std::uint64_t lastUsedFrame = 0;
};
class GlMaterialProgramCache
{
  public:
    using TextureUploader = std::function<std::uint32_t(const std::shared_ptr<TextureResource>&, bool, std::uint64_t)>;
    explicit GlMaterialProgramCache(TextureUploader uploader);
    ~GlMaterialProgramCache();
    bool initialize();
    void setTemplates(MaterialTemplateSources sources);
    bool validateTemplates(const MaterialTemplateSources& sources);
    void retireUnused();
    void setFrameBindings(const std::array<std::uint32_t, 3>& shadows, std::uint32_t lightData,
                          std::uint32_t clusterData, std::size_t cascade, Vec2 viewport);
    GlMaterialProgram* getOrCreate(const CompiledMaterial&, MaterialPass, MaterialGeometry);
    bool bind(const MaterialRenderProxy&, const FrameView&, MaterialPass, MaterialGeometry);
    void bindObject(const RenderItem&, const FrameView&, MaterialGeometry);
    std::size_t getCompileCount(MaterialPass) const;
    const std::vector<MaterialDiagnostic>& GetDiagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace tinyengine
