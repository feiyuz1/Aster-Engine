#include "gl_material_program_cache.h"
#include "tinyengine/render/shader_pipeline.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <type_traits>
#include <unordered_set>

namespace tinyengine
{
struct GlMaterialProgramCache::Impl
{
    PFNGLCREATESHADERPROC CreateShader{};
    PFNGLSHADERSOURCEPROC ShaderSource{};
    PFNGLCOMPILESHADERPROC CompileShader{};
    PFNGLGETSHADERIVPROC GetShaderiv{};
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog{};
    PFNGLDELETESHADERPROC DeleteShader{};
    PFNGLCREATEPROGRAMPROC CreateProgram{};
    PFNGLATTACHSHADERPROC AttachShader{};
    PFNGLLINKPROGRAMPROC LinkProgram{};
    PFNGLGETPROGRAMIVPROC GetProgramiv{};
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog{};
    PFNGLDELETEPROGRAMPROC DeleteProgram{};
    PFNGLUSEPROGRAMPROC UseProgram{};
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation{};
    PFNGLUNIFORM1IPROC Uniform1i{};
    PFNGLUNIFORM1FPROC Uniform1f{};
    PFNGLUNIFORM2FPROC Uniform2f{};
    PFNGLUNIFORM3FPROC Uniform3f{};
    PFNGLUNIFORM3IPROC Uniform3i{};
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv{};
    PFNGLGENBUFFERSPROC GenBuffers{};
    PFNGLDELETEBUFFERSPROC DeleteBuffers{};
    PFNGLBINDBUFFERPROC BindBuffer{};
    PFNGLBUFFERDATAPROC BufferData{};
    PFNGLBUFFERSUBDATAPROC BufferSubData{};
    PFNGLBINDBUFFERBASEPROC BindBufferBase{};
    PFNGLGETUNIFORMBLOCKINDEXPROC GetUniformBlockIndex{};
    PFNGLGETACTIVEUNIFORMBLOCKIVPROC GetActiveUniformBlockiv{};
    PFNGLUNIFORMBLOCKBINDINGPROC UniformBlockBinding{};
    PFNGLGETUNIFORMINDICESPROC GetUniformIndices{};
    PFNGLGETACTIVEUNIFORMSIVPROC GetActiveUniformsiv{};
    PFNGLACTIVETEXTUREPROC ActiveTexture{};
    TextureUploader upload;
    MaterialTemplateSources templates;
    std::unordered_map<std::string, std::unique_ptr<GlMaterialProgram>> programs;
    std::unordered_set<std::string> failed, warned;
    std::vector<MaterialDiagnostic> diagnostics;
    std::array<std::size_t, 2> count{};
    std::uint64_t frame = 0;
    struct FastEntry
    {
        std::weak_ptr<const CompiledMaterial> owner;
        std::array<GlMaterialProgram*, 6> variants{};
    };
    std::unordered_map<const CompiledMaterial*, FastEntry> fast;
    GLuint numericBuffer = 0, fallback[2]{};
    std::array<std::uint32_t, 3> shadows{};
    std::uint32_t lightData = 0, clusterData = 0;
    std::size_t cascade = 0;
    Vec2 viewport{};
    GlMaterialProgram* current = nullptr;
    explicit Impl(TextureUploader callback) : upload(std::move(callback)) {}
    ~Impl()
    {
        if (DeleteProgram)
            for (const auto& program : programs)
                DeleteProgram(program.second->handle);
        if (DeleteBuffers && numericBuffer)
            DeleteBuffers(1, &numericBuffer);
        if (fallback[0] || fallback[1])
            glDeleteTextures(2, fallback);
    }
    void error(const std::string& message)
    {
        diagnostics.push_back({MaterialDiagnosticSeverity::Error, {}, 0, -1, message});
    }
    GLint location(const char* name)
    {
        const auto found = current->locations.find(name);
        if (found != current->locations.end())
            return found->second;
        const auto location = GetUniformLocation(current->handle, name);
        current->locations.emplace(name, location);
        return location;
    }
    void matrix(const char* name, const Mat4& matrix)
    {
        UniformMatrix4fv(location(name), 1, GL_FALSE, matrix.Data());
    }
    GLuint compileStage(GLenum type, const std::string& source)
    {
        const GLuint shader = CreateShader(type);
        const char* text = source.c_str();
        ShaderSource(shader, 1, &text, nullptr);
        CompileShader(shader);
        GLint success = 0;
        GetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLint size = 0;
            GetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
            std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
            GetShaderInfoLog(shader, size, nullptr, log.data());
            error("Material GLSL compile failed: " + log);
            DeleteShader(shader);
            return 0;
        }
        return shader;
    }
    bool validateLayout(GLuint program, const MaterialParameterLayout& layout)
    {
        const GLuint block = GetUniformBlockIndex(program, "MaterialParameters");
        if (block != GL_INVALID_INDEX)
        {
            GLint bytes = 0;
            GetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &bytes);
            const char* name = "te_parameters[0]";
            GLuint index = GL_INVALID_INDEX;
            GetUniformIndices(program, 1, &name, &index);
            GLint offset = -1, stride = -1;
            if (index != GL_INVALID_INDEX)
            {
                GetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset);
                GetActiveUniformsiv(program, 1, &index, GL_UNIFORM_ARRAY_STRIDE, &stride);
            }
            if (bytes != 1024 || offset != 0 || stride != 16)
            {
                error("Material std140 layout differs from 64 Vec4 slots");
                return false;
            }
            UniformBlockBinding(program, block, 2);
        }
        for (const auto& slot : layout.textureSlots)
        {
            const auto text = "te_texture_" + std::to_string(slot.slot);
            const char* name = text.c_str();
            GLuint index = GL_INVALID_INDEX;
            GetUniformIndices(program, 1, &name, &index);
            if (index == GL_INVALID_INDEX)
                continue;
            GLint type = 0;
            GetActiveUniformsiv(program, 1, &index, GL_UNIFORM_TYPE, &type);
            if (type != GL_SAMPLER_2D)
            {
                error("Material texture slot is not sampler2D: " + slot.name);
                return false;
            }
        }
        return true;
    }
    GLuint Texture(const std::shared_ptr<TextureResource>& resource, bool srgb, std::uint64_t version = 0)
    {
        if (!resource || !resource->isReady())
            return 0;
        GLint active = 0, binding = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        const auto result = upload(resource, srgb, version);
        ActiveTexture(static_cast<GLenum>(active));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(binding));
        return result;
    }
    void bindTexture(int unit, GLuint texture, GLenum target = GL_TEXTURE_2D)
    {
        ActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, texture);
    }
};
GlMaterialProgramCache::GlMaterialProgramCache(TextureUploader uploader)
    : m_impl(std::make_unique<Impl>(std::move(uploader)))
{
}
GlMaterialProgramCache::~GlMaterialProgramCache() = default;
bool GlMaterialProgramCache::initialize()
{
    auto& p = *m_impl;
    const auto load = [](auto& function, const char* name)
    {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(SDL_GL_GetProcAddress(name));
        return function != nullptr;
    };
#define TE_LOAD(name)                                                                                                  \
    if (!load(p.name, "gl" #name))                                                                                     \
    {                                                                                                                  \
        p.error("Missing GL function gl" #name);                                                                       \
        return false;                                                                                                  \
    }
    TE_LOAD(CreateShader)
    TE_LOAD(ShaderSource)
    TE_LOAD(CompileShader)
    TE_LOAD(GetShaderiv)
    TE_LOAD(GetShaderInfoLog)
    TE_LOAD(DeleteShader)
    TE_LOAD(CreateProgram)
    TE_LOAD(AttachShader)
    TE_LOAD(LinkProgram)
    TE_LOAD(GetProgramiv)
    TE_LOAD(GetProgramInfoLog)
    TE_LOAD(DeleteProgram)
    TE_LOAD(UseProgram)
    TE_LOAD(GetUniformLocation)
    TE_LOAD(Uniform1i)
    TE_LOAD(Uniform1f)
    TE_LOAD(Uniform2f)
    TE_LOAD(Uniform3f)
    TE_LOAD(Uniform3i)
    TE_LOAD(UniformMatrix4fv)
    TE_LOAD(GenBuffers)
    TE_LOAD(DeleteBuffers)
    TE_LOAD(BindBuffer)
    TE_LOAD(BufferData)
    TE_LOAD(BufferSubData)
    TE_LOAD(BindBufferBase)
    TE_LOAD(GetUniformBlockIndex)
    TE_LOAD(GetActiveUniformBlockiv)
    TE_LOAD(UniformBlockBinding)
    TE_LOAD(GetUniformIndices)
    TE_LOAD(GetActiveUniformsiv)
    TE_LOAD(ActiveTexture)
#undef TE_LOAD
    GLint units = 0;
    GLint blockBytes = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &blockBytes);
    if (units < 16 || blockBytes < 1024)
    {
        p.error("Material rendering requires 16 fragment texture units and a 1024-byte UBO");
        return false;
    }
    p.GenBuffers(1, &p.numericBuffer);
    p.BindBuffer(GL_UNIFORM_BUFFER, p.numericBuffer);
    const std::array<Vec4, 64> zeros{};
    p.BufferData(GL_UNIFORM_BUFFER, sizeof(zeros), zeros.data(), GL_DYNAMIC_DRAW);
    glGenTextures(2, p.fallback);
    for (int index = 0; index < 2; ++index)
    {
        glBindTexture(GL_TEXTURE_2D, p.fallback[index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        const float pixel[] = {index ? .5f : 1.f, index ? .5f : 1.f, 1.f, 1.f};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, pixel);
    }
    p.templates = ShaderPipeline::loadMaterialTemplates();
    return true;
}
void GlMaterialProgramCache::setTemplates(MaterialTemplateSources sources)
{
    m_impl->templates = std::move(sources);
    m_impl->fast.clear();
}
void GlMaterialProgramCache::retireUnused()
{
    auto& p = *m_impl;
    ++p.frame;
    std::unordered_set<GlMaterialProgram*> live;
    for (auto it = p.fast.begin(); it != p.fast.end();)
    {
        if (it->second.owner.expired())
            it = p.fast.erase(it);
        else
        {
            for (auto* variant : it->second.variants)
                if (variant)
                    live.insert(variant);
            ++it;
        }
    }
    std::vector<std::pair<std::uint64_t, std::string>> unused;
    for (const auto& entry : p.programs)
        if (!live.count(entry.second.get()))
            unused.emplace_back(entry.second->lastUsedFrame, entry.first);
    std::sort(unused.begin(), unused.end());
    for (std::size_t i = 0; i < unused.size(); ++i)
        if (p.frame > unused[i].first + 2 || i + 64 < unused.size())
        {
            const auto entry = p.programs.find(unused[i].second);
            if (p.current == entry->second.get())
                p.current = nullptr;
            p.DeleteProgram(entry->second->handle);
            p.programs.erase(entry);
        }
    if (p.failed.size() > 64)
        p.failed.clear();
}
bool GlMaterialProgramCache::validateTemplates(const MaterialTemplateSources& sources)
{
    auto& p = *m_impl;
    auto previous = p.templates;
    p.templates = sources;
    std::vector<std::shared_ptr<const CompiledMaterial>> materials;
    for (const auto& entry : p.fast)
        if (auto material = entry.second.owner.lock())
            materials.push_back(std::move(material));
    materials.push_back(MaterialCompiler{}.compile(makeBuiltinPbrDefinition()).material);
    bool valid = true;
    for (const auto& material : materials)
        for (auto pass : {MaterialPass::Base, MaterialPass::Shadow})
            for (auto geometry : {MaterialGeometry::Static, MaterialGeometry::Skinned, MaterialGeometry::Instanced})
                valid = getOrCreate(*material, pass, geometry) != nullptr && valid;
    p.templates = std::move(previous);
    return valid;
}
void GlMaterialProgramCache::setFrameBindings(const std::array<std::uint32_t, 3>& shadows, std::uint32_t lightData,
                                              std::uint32_t clusterData, std::size_t cascade, Vec2 viewport)
{
    m_impl->shadows = shadows;
    m_impl->lightData = lightData;
    m_impl->clusterData = clusterData;
    m_impl->cascade = cascade;
    m_impl->viewport = viewport;
}
GlMaterialProgram* GlMaterialProgramCache::getOrCreate(const CompiledMaterial& material, MaterialPass pass,
                                                       MaterialGeometry geometry)
{
    auto& p = *m_impl;
    const auto shaders = MaterialCompiler{}.generatePass(material, pass, geometry, p.templates);
    const auto key = material.canonicalProgramContent + '\n' + shaders.vertex + '\n' + shaders.fragment;
    const auto found = p.programs.find(key);
    if (found != p.programs.end())
    {
        found->second->lastUsedFrame = p.frame;
        return found->second.get();
    }
    if (p.failed.count(key))
        return nullptr;
    if (!shaders.error.empty())
    {
        p.error(shaders.error);
        p.failed.insert(key);
        return nullptr;
    }
    const GLuint vertex = p.compileStage(GL_VERTEX_SHADER, shaders.vertex);
    const GLuint fragment = vertex ? p.compileStage(GL_FRAGMENT_SHADER, shaders.fragment) : 0;
    if (!vertex || !fragment)
    {
        if (vertex)
            p.DeleteShader(vertex);
        p.failed.insert(key);
        return nullptr;
    }
    const GLuint handle = p.CreateProgram();
    p.AttachShader(handle, vertex);
    p.AttachShader(handle, fragment);
    p.LinkProgram(handle);
    p.DeleteShader(vertex);
    p.DeleteShader(fragment);
    GLint success = 0;
    p.GetProgramiv(handle, GL_LINK_STATUS, &success);
    if (!success)
    {
        GLint size = 0;
        p.GetProgramiv(handle, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        p.GetProgramInfoLog(handle, size, nullptr, log.data());
        p.error("Material GL link failed: " + log);
    }
    if (!success || !p.validateLayout(handle, material.layout))
    {
        p.DeleteProgram(handle);
        p.failed.insert(key);
        return nullptr;
    }
    auto program = std::make_unique<GlMaterialProgram>();
    program->handle = handle;
    program->pass = pass;
    program->geometry = geometry;
    program->lastUsedFrame = p.frame;
    auto* result = program.get();
    p.programs.emplace(key, std::move(program));
    ++p.count[static_cast<std::size_t>(pass)];
    return result;
}
bool GlMaterialProgramCache::bind(const MaterialRenderProxy& proxy, const FrameView& view, MaterialPass pass,
                                  MaterialGeometry geometry)
{
    auto& p = *m_impl;
    if (!proxy.compiled || proxy.numericSlots.size() != proxy.compiled->layout.numericSlots.size() ||
        proxy.numericSlots.size() > 64)
        return false;
    auto& entry = p.fast[proxy.compiled.get()];
    if (entry.owner.lock() != proxy.compiled)
    {
        entry = Impl::FastEntry{};
        entry.owner = proxy.compiled;
    }
    const auto variant = static_cast<std::size_t>(pass) * 3 + static_cast<std::size_t>(geometry);
    if (!entry.variants[variant])
        entry.variants[variant] = getOrCreate(*proxy.compiled, pass, geometry);
    p.current = entry.variants[variant];
    if (!p.current)
        return false;
    p.current->lastUsedFrame = p.frame;
    p.UseProgram(p.current->handle);
    std::array<Vec4, 64> slots{};
    std::copy(proxy.numericSlots.begin(), proxy.numericSlots.end(), slots.begin());
    p.BindBuffer(GL_UNIFORM_BUFFER, p.numericBuffer);
    p.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(slots), slots.data());
    p.BindBufferBase(GL_UNIFORM_BUFFER, 2, p.numericBuffer);
    for (int unit = 0; unit < 8; ++unit)
        p.bindTexture(unit, p.fallback[0]);
    for (const auto& slot : proxy.compiled->layout.textureSlots)
    {
        const auto binding = std::find_if(proxy.textures.begin(), proxy.textures.end(),
                                          [&](const auto& t) { return t.slot == slot.slot; });
        const GLuint texture =
            binding == proxy.textures.end()
                ? 0
                : p.Texture(binding->resource, slot.sampling == TextureSampling::SRgb, binding->version);
        if (!texture && binding != proxy.textures.end() && !binding->requestedPath.empty() &&
            p.warned.insert(binding->requestedPath + ':' + std::to_string(static_cast<int>(slot.sampling))).second)
            p.diagnostics.push_back({MaterialDiagnosticSeverity::Warning, binding->requestedPath, 0, -1,
                                     "Missing texture; using semantic fallback"});
        p.bindTexture(static_cast<int>(slot.slot),
                      texture ? texture : p.fallback[slot.sampling == TextureSampling::Normal ? 1 : 0]);
        p.Uniform1i(p.location(("te_texture_" + std::to_string(slot.slot)).c_str()), static_cast<GLint>(slot.slot));
    }
    p.Uniform1i(p.location("uMaterialTwoSided"), proxy.state.twoSided ? 1 : 0);
    p.Uniform1i(p.location("uViewportRenderMode"), static_cast<int>(view.renderMode));
    p.Uniform1f(p.location("uMaterialMaskCutoff"), proxy.state.maskCutoff);
    p.matrix("uViewProjection", view.projection * view.view);
    p.matrix("uPreviousViewProjection", view.previousViewProjection);
    p.matrix("uView", view.view);
    p.matrix("uLightViewProjection", view.shadowViewProjections[std::min<std::size_t>(p.cascade, 2)]);
    for (int index = 0; index < 3; ++index)
    {
        const auto suffix = '[' + std::to_string(index) + ']';
        p.matrix(("uShadowViewProjections" + suffix).c_str(), view.shadowViewProjections[index]);
        p.Uniform1f(p.location(("uShadowCascadeSplits" + suffix).c_str()), view.shadowCascadeSplits[index]);
        p.Uniform1i(p.location(("uShadowMaps" + suffix).c_str()), 8 + index);
        p.bindTexture(8 + index, p.shadows[index]);
    }
    const auto irradiance = view.environmentIrradiance ? view.environmentIrradiance : view.environmentTexture;
    const auto prefilter = view.environmentPrefilter ? view.environmentPrefilter : view.environmentTexture;
    const std::shared_ptr<TextureResource> environment[] = {irradiance, prefilter, view.environmentBrdfLut};
    const char* names[] = {"uEnvironmentIrradiance", "uEnvironmentPrefilter", "uEnvironmentBrdfLut"};
    bool environmentReady = true;
    for (int index = 0; index < 3; ++index)
    {
        const auto texture = p.Texture(environment[index], false);
        environmentReady = environmentReady && texture != 0;
        p.bindTexture(11 + index, texture ? texture : p.fallback[0]);
        p.Uniform1i(p.location(names[index]), 11 + index);
    }
    p.Uniform1i(p.location("uHasEnvironment"), environmentReady ? 1 : 0);
    p.bindTexture(14, p.lightData, GL_TEXTURE_BUFFER);
    p.bindTexture(15, p.clusterData, GL_TEXTURE_BUFFER);
    p.Uniform1i(p.location("uLightData"), 14);
    p.Uniform1i(p.location("uClusterData"), 15);
    p.Uniform1i(p.location("uLightCount"), view.lightCount);
    p.Uniform3f(p.location("uCameraPosition"), view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z);
    p.Uniform3i(p.location("uClusterDimensions"), static_cast<GLint>(view.clusteredLights.tilesX),
                static_cast<GLint>(view.clusteredLights.tilesY), static_cast<GLint>(view.clusteredLights.slicesZ));
    p.Uniform2f(p.location("uClusterDepthRange"), view.clusteredLights.nearPlane, view.clusteredLights.farPlane);
    p.Uniform2f(p.location("uViewportSize"), p.viewport.x, p.viewport.y);
    p.ActiveTexture(GL_TEXTURE0);
    return true;
}
void GlMaterialProgramCache::bindObject(const RenderItem& item, const FrameView& view, MaterialGeometry geometry)
{
    auto& p = *m_impl;
    if (!p.current)
        return;
    p.matrix("uModel", item.model);
    p.matrix("uPreviousModel", item.previousModel);
    p.matrix("uMvp", view.projection * view.view * item.model);
    p.Uniform1i(p.location("uInstanced"), geometry == MaterialGeometry::Instanced ? 1 : 0);
    std::array<Mat4, 64> bones;
    for (auto& bone : bones)
        bone = Mat4::identity();
    for (std::size_t i = 0; i < item.skinMatrices.size() && i < bones.size(); ++i)
        bones[i] = item.skinMatrices[i];
    p.UniformMatrix4fv(p.location("uBones[0]"), 64, GL_FALSE, bones[0].Data());
}
std::size_t GlMaterialProgramCache::getCompileCount(MaterialPass pass) const
{
    return m_impl->count[static_cast<std::size_t>(pass)];
}
const std::vector<MaterialDiagnostic>& GlMaterialProgramCache::GetDiagnostics() const
{
    return m_impl->diagnostics;
}
} // namespace tinyengine
