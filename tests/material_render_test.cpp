#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/resource/pak_archive.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

using namespace tinyengine;
namespace
{
int failures = 0;
const std::string root = "saved/MaterialRender/";
void check(bool condition, const char* label)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
    failures += !condition;
}
bool isClose(float a, float b, float tolerance = .004f)
{
    return std::abs(a - b) < tolerance;
}
MaterialDefinitionData colorDefinition()
{
    MaterialDefinitionData data;
    data.sourcePath = "gpu:color";
    data.graph.setFormatVersion(2);
    data.renderState.shadingModel = MaterialShadingModel::Unlit;
    data.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    MaterialParameterDecl p;
    p.name = "Color";
    p.type = MaterialParameterType::Vec4;
    p.defaultValue = Vec4{1, 0, 0, 1};
    data.parameters.push_back(p);
    const auto parameter = data.graph.addNode(MaterialNodeType::VectorParameter, "Color");
    data.graph.GetNodes().back().parameterName = "Color";
    const auto mask = data.graph.addNode(MaterialNodeType::ComponentMask, "RGB");
    data.graph.GetNodes().back().channelMask = "xyz";
    data.graph.connect(parameter, mask, 0);
    data.graph.connect(mask, 1, 0);
    return data;
}
std::shared_ptr<MaterialRenderProxy> Proxy(const MaterialDefinitionData& definition)
{
    const auto compiled = MaterialCompiler{}.compile(definition);
    check(compiled.Success(), "CPU candidate for GPU test compiles");
    if (!compiled.Success())
        throw std::runtime_error("GPU fixture graph failed");
    auto proxy = std::make_shared<MaterialRenderProxy>();
    proxy->compiled = compiled.material;
    proxy->state = definition.renderState;
    for (const auto& slot : proxy->compiled->layout.numericSlots)
    {
        const auto p = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                    [&](const auto& q) { return q.name == slot.name; });
        proxy->numericSlots.push_back(materialNumericSlot(p->defaultValue));
    }
    for (const auto& slot : proxy->compiled->layout.textureSlots)
        proxy->textures.push_back({slot.name, slot.slot, {}, slot.sampling});
    return proxy;
}
MaterialDefinitionData textureDefinition(TextureSampling sampling)
{
    auto data = colorDefinition();
    data.parameters.clear();
    data.graph.GetNodes().resize(1);
    data.graph.GetLinks().clear();
    MaterialParameterDecl p;
    p.name = "Map";
    p.type = MaterialParameterType::Texture2D;
    p.defaultValue = std::string{};
    p.sampling = sampling;
    data.parameters.push_back(p);
    const auto handle = data.graph.addNode(MaterialNodeType::TextureParameter, "Map");
    data.graph.GetNodes().back().parameterName = "Map";
    const auto sample = data.graph.addNode(MaterialNodeType::TextureSample, "Sample");
    data.graph.connect(handle, sample, 0);
    const auto mask = data.graph.addNode(MaterialNodeType::ComponentMask, "RGB");
    data.graph.GetNodes().back().channelMask = "xyz";
    data.graph.connect(sample, mask, 0);
    data.graph.connect(mask, 1, 0);
    return data;
}
FrameView view()
{
    FrameView view;
    view.lightCount = 1;
    view.lights[0].intensity = 0;
    view.clusteredLights.tilesX = 1;
    view.clusteredLights.tilesY = 1;
    view.clusteredLights.slicesZ = 1;
    view.clusteredLights.offsets = {0};
    view.clusteredLights.counts = {1};
    view.clusteredLights.lightIndices = {0};
    for (auto& matrix : view.shadowViewProjections)
        matrix = Mat4::identity();
    view.shadowCascadeSplits = {1, 10, 100};
    return view;
}
Vec4 draw(SdlGlRenderDevice& device, RenderItem& item, const FrameView& view)
{
    device.beginFrame(view);
    device.draw(item);
    if (!device.GetLastShaderError().empty())
        std::cout << "Material draw diagnostic: " << device.GetLastShaderError() << '\n';
    const auto hdr = device.ReadHdrAttachmentForTests(0);
    check(hdr.size() == 64 * 64, "read real linear HDR target");
    if (hdr.size() != 64 * 64)
        return {};
    return hdr[32 * 64 + 32];
}
void testViewportModes(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    graph.renderState.shadingModel = MaterialShadingModel::Lit;
    item.materialProxy = Proxy(graph);
    auto viewValue = view();
    viewValue.renderMode = ViewportRenderMode::Unlit;
    const auto unlit = draw(device, item, viewValue);
    check(isClose(unlit.x, 1) && isClose(unlit.y, 0) && isClose(unlit.z, 0),
          "Unlit viewport shows base color without scene lighting");
    auto bright = viewValue;
    bright.lights[0].intensity = 100;
    const auto unchanged = draw(device, item, bright);
    check(isClose(unchanged.x, unlit.x) && isClose(unchanged.y, unlit.y), "Unlit viewport ignores light intensity");
    device.beginFrame(viewValue);
    device.drawInstanced({item, item});
    auto pixels = device.ReadHdrAttachmentForTests(0);
    check(isClose(pixels[32 * 64 + 32].x, 1), "instanced materials honor Unlit viewport");
    viewValue.renderMode = ViewportRenderMode::Wireframe;
    device.beginFrame(viewValue);
    device.draw(item);
    pixels = device.ReadHdrAttachmentForTests(0);
    int edges = 0;
    for (const auto& p : pixels)
        if (p.x + p.y + p.z > .1f)
            ++edges;
    check(edges > 30 && edges < 600 && pixels[32 * 64 + 40].x < .01f,
          "Wireframe draws polygon edges with empty interiors");
    GLint polygon[2]{};
    glGetIntegerv(GL_POLYGON_MODE, polygon);
    check(polygon[0] == GL_FILL, "Wireframe restores fill state for post processing and editor UI");
    viewValue.renderMode = ViewportRenderMode::Unlit;
    const auto restored = draw(device, item, viewValue);
    check(isClose(restored.x, 1), "switching from Wireframe restores filled geometry");
    viewValue.renderMode = ViewportRenderMode::Lit;
    const auto lit = draw(device, item, viewValue);
    check(lit.x < .9f, "returning to Lit restores material lighting");
    const auto originalMesh = item.mesh;
    item.mesh = std::make_shared<MeshResource>();
    check(item.mesh->load(root + "weighted.mesh"), "load viewport skinning fixture");
    item.skinMatrices = {Mat4::identity()};
    viewValue.renderMode = ViewportRenderMode::Unlit;
    const auto skinned = draw(device, item, viewValue);
    check(isClose(skinned.x, 1), "skinned material honors Unlit viewport");
    item.skinMatrices.clear();
    item.mesh = originalMesh;
    auto preview = std::make_shared<MaterialRenderProxy>(*item.materialProxy);
    preview->numericSlots[0] = {.25f, 0, 0, 1};
    item.materialProxy = preview;
    device.setExposure(8);
    device.setBloomEnabled(true);
    device.setGtaoEnabled(true);
    device.setTaaEnabled(true);
    device.beginFrame(viewValue);
    device.draw(item);
    for (auto pass : {PostProcessPass::HierarchicalDepth, PostProcessPass::Gtao, PostProcessPass::Bloom,
                      PostProcessPass::TemporalResolve, PostProcessPass::ToneMap})
        device.executePostProcess(pass);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(device.getSceneColorTexture()));
    std::array<Vec4, 64 * 64> output{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, output.data());
    check(isClose(output[32 * 64 + 32].x, .533f, .012f) && output[32 * 64 + 32].y < .01f,
          "Unlit final image bypasses exposure, bloom and ambient occlusion");
    check(device.isBloomEnabled() && device.isGtaoEnabled() && device.isTaaEnabled() &&
              isClose(device.getExposure(), 8),
          "diagnostic viewport modes preserve rendering preferences");
    device.setExposure(1);
    device.setBloomEnabled(false);
    device.setGtaoEnabled(false);
    device.setTaaEnabled(false);
    item.materialProxy.reset();
    item.material = std::make_shared<MaterialResource>();
    check(item.material->load(root + "pbr.mat"), "load legacy viewport material");
    const auto legacy = draw(device, item, viewValue);
    check(isClose(legacy.x, .4f) && isClose(legacy.y, .2f) && isClose(legacy.z, .1f),
          "legacy material honors Unlit viewport");
    device.beginFrame(viewValue);
    device.drawInstanced({item, item});
    pixels = device.ReadHdrAttachmentForTests(0);
    check(isClose(pixels[32 * 64 + 32].x, .4f), "legacy instancing honors Unlit viewport");
    viewValue.renderMode = ViewportRenderMode::Wireframe;
    device.beginFrame(viewValue);
    device.drawInstanced({item, item});
    pixels = device.ReadHdrAttachmentForTests(0);
    edges = 0;
    for (const auto& p : pixels)
        if (p.x + p.y + p.z > .1f)
            ++edges;
    check(edges > 30 && edges < 600, "legacy instancing honors Wireframe viewport");
    item.material.reset();
    item.materialProxy.reset();
}

void testSharedPrograms(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    item.materialProxy = Proxy(graph);
    const auto before = device.getMaterialProgramCompileCountForTests(MaterialPass::Base);
    auto red = draw(device, item, view());
    auto blue = std::make_shared<MaterialRenderProxy>(*item.materialProxy);
    blue->numericSlots[0] = {0, 0, 1, 1};
    item.materialProxy = blue;
    auto bluePixel = draw(device, item, view());
    check(isClose(red.x, 1) && isClose(red.z, 0) && isClose(bluePixel.x, 0) && isClose(bluePixel.z, 1),
          "shared program renders independent red and blue values");
    check(device.getMaterialProgramCompileCountForTests(MaterialPass::Base) == before + 1,
          "red and blue compile one Base variant");
    auto changed = colorDefinition();
    changed.graph.GetNodes().back().channelMask = "zyx";
    item.materialProxy = Proxy(changed);
    draw(device, item, view());
    check(device.getMaterialProgramCompileCountForTests(MaterialPass::Base) == before + 2,
          "changed graph expression compiles a new program");
}
void testParameterUpload(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    for (const auto& entry : std::map<std::string, MaterialParameterValue>{
             {"FloatValue", .125f}, {"Vec2Value", Vec2{2, 3}}, {"Vec3Value", Vec3{4, 5, 6}}})
    {
        MaterialParameterDecl p;
        p.name = entry.first;
        p.type = getMaterialParameterType(entry.second);
        p.defaultValue = entry.second;
        graph.parameters.push_back(p);
    }
    auto proxy = Proxy(graph);
    item.materialProxy = proxy;
    draw(device, item, view());
    using GetIndexed = void(APIENTRY*)(GLenum, GLuint, GLint*);
    using BindBuffer = void(APIENTRY*)(GLenum, GLuint);
    using ReadBuffer = void(APIENTRY*)(GLenum, std::ptrdiff_t, std::ptrdiff_t, void*);
    const auto get = reinterpret_cast<GetIndexed>(SDL_GL_GetProcAddress("glGetIntegeri_v"));
    const auto bind = reinterpret_cast<BindBuffer>(SDL_GL_GetProcAddress("glBindBuffer"));
    const auto read = reinterpret_cast<ReadBuffer>(SDL_GL_GetProcAddress("glGetBufferSubData"));
    check(get && bind && read, "GPU UBO readback functions available");
    if (!get || !bind || !read)
        return;
    GLint buffer = 0;
    get(0x8A28, 2, &buffer);
    check(buffer != 0, "numeric UBO is bound at binding 2");
    std::array<Vec4, 64> values{};
    if (!buffer)
        return;
    bind(0x8A11, static_cast<GLuint>(buffer));
    read(0x8A11, 0, sizeof(values), values.data());
    const std::map<std::string, Vec4> expected = {{"Color", {1, 0, 0, 1}},
                                                  {"FloatValue", {.125f, 0, 0, 0}},
                                                  {"Vec2Value", {2, 3, 0, 0}},
                                                  {"Vec3Value", {4, 5, 6, 0}}};
    for (const auto& slot : proxy->compiled->layout.numericSlots)
    {
        const auto v = values[slot.slot], e = expected.at(slot.name);
        check(isClose(v.x, e.x) && isClose(v.y, e.y) && isClose(v.z, e.z) && isClose(v.w, e.w),
              "GPU contains correctly padded typed parameter slot");
    }
}
void testTextureSemantics(SdlGlRenderDevice& device, RenderItem& item)
{
    auto texture = std::make_shared<TextureResource>();
    check(texture->load(root + "gray.ppm"), "load gray texture");
    auto linear = Proxy(textureDefinition(TextureSampling::Linear));
    linear->textures[0].resource = texture;
    item.materialProxy = linear;
    auto linearPixel = draw(device, item, view());
    auto srgb = Proxy(textureDefinition(TextureSampling::SRgb));
    srgb->textures[0].resource = texture;
    item.materialProxy = srgb;
    auto srgbPixel = draw(device, item, view());
    check(isClose(linearPixel.x, 128.f / 255.f) && isClose(srgbPixel.x, .21586f),
          "same image samples correctly as Linear and sRGB");
    const auto before = device.getMaterialProgramCompileCountForTests(MaterialPass::Base);
    auto darker = std::make_shared<TextureResource>();
    check(darker->load(root + "dark.ppm"), "load replacement texture");
    auto changed = std::make_shared<MaterialRenderProxy>(*linear);
    changed->textures[0].resource = darker;
    item.materialProxy = changed;
    const auto darkerPixel = draw(device, item, view());
    check(isClose(darkerPixel.x, 64.f / 255.f) &&
              device.getMaterialProgramCompileCountForTests(MaterialPass::Base) == before,
          "texture path/value change reuses program");
}
void testFallbackTexture(SdlGlRenderDevice& device, RenderItem& item)
{
    item.materialProxy = Proxy(textureDefinition(TextureSampling::SRgb));
    const auto color = draw(device, item, view());
    check(isClose(color.x, 1) && isClose(color.y, 1) && isClose(color.z, 1),
          "missing color texture falls back to white");
    auto normalGraph = textureDefinition(TextureSampling::Normal);
    const auto decode = normalGraph.graph.addNode(MaterialNodeType::NormalDecode, "Decode");
    normalGraph.graph.connect(normalGraph.graph.GetNodes()[3].id, decode, 0);
    normalGraph.graph.connect(decode, 1, 0);
    item.materialProxy = Proxy(normalGraph);
    const auto normal = draw(device, item, view());
    check(isClose(normal.x, 0) && isClose(normal.y, 0) && isClose(normal.z, 1),
          "missing normal texture decodes to exact flat normal");
    auto missing = Proxy(textureDefinition(TextureSampling::SRgb));
    missing->textures[0].requestedPath = "missing-test.png";
    item.materialProxy = missing;
    const auto before = device.GetMaterialDiagnosticsForTests().size();
    draw(device, item, view());
    draw(device, item, view());
    check(device.GetMaterialDiagnosticsForTests().size() == before + 1,
          "missing texture warns once across repeated draws");
}
void testTextureBudget(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = textureDefinition(TextureSampling::Linear);
    for (int i = 1; i < 8; ++i)
    {
        MaterialParameterDecl p;
        p.name = "UnusedMap" + std::to_string(i);
        p.type = MaterialParameterType::Texture2D;
        p.defaultValue = std::string{};
        graph.parameters.push_back(p);
    }
    auto proxy = Proxy(graph);
    item.materialProxy = proxy;
    auto viewValue = view();
    auto environment = std::make_shared<TextureResource>();
    environment->load(root + "gray.ppm");
    viewValue.environmentIrradiance = environment;
    viewValue.environmentPrefilter = environment;
    viewValue.environmentBrdfLut = environment;
    draw(device, item, viewValue);
    using ActiveTexture = void(APIENTRY*)(GLenum);
    const auto active = reinterpret_cast<ActiveTexture>(SDL_GL_GetProcAddress("glActiveTexture"));
    bool complete = active != nullptr;
    if (active)
        for (int unit = 0; unit < 16; ++unit)
        {
            active(GL_TEXTURE0 + unit);
            GLint binding = 0;
            glGetIntegerv(unit < 14 ? GL_TEXTURE_BINDING_2D : 0x8C2C, &binding);
            complete = complete && binding != 0;
        }
    check(complete, "8 material, 3 shadow, 3 environment and 2 buffer units remain bound together");
    if (active)
        active(GL_TEXTURE0);
    check(glGetError() == GL_NO_ERROR, "material binding and draws generate no GL errors");
}
void testPassClippingAndState(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    graph.renderState.blendMode = MaterialBlendMode::Masked;
    graph.renderState.twoSided = true;
    auto uv = graph.graph.addNode(MaterialNodeType::TexCoord, "UV");
    auto x = graph.graph.addNode(MaterialNodeType::ComponentMask, "X");
    graph.graph.GetNodes().back().channelMask = "x";
    graph.graph.connect(uv, x, 0);
    graph.graph.connect(x, 1, 6);
    item.materialProxy = Proxy(graph);
    draw(device, item, view());
    auto hdr = device.ReadHdrAttachmentForTests(0);
    check(hdr[32 * 64 + 20].x < .02f && hdr[32 * 64 + 44].x > .9f, "Masked Base clips UV hole and keeps solid half");
    device.beginShadowPass(view(), 0);
    device.drawShadow(item);
    auto left = device.readShadowDepthForTests(640, 1024), right = device.readShadowDepthForTests(1408, 1024);
    device.endShadowPass();
    check(left > .99f && right < .6f, "Masked Shadow clips the same UV hole");
    auto opaque = colorDefinition();
    opaque.renderState.twoSided = true;
    item.materialProxy = Proxy(opaque);
    item.model = scaleMatrix({-1, 1, 1});
    auto back = draw(device, item, view());
    opaque.renderState.twoSided = false;
    item.materialProxy = Proxy(opaque);
    auto culled = draw(device, item, view());
    opaque.renderState.twoSided = true;
    item.materialProxy = Proxy(opaque);
    auto again = draw(device, item, view());
    check(back.x > .9f && culled.x < .02f && again.x > .9f, "TwoSided state alternates without leaking cull state");
    item.model = Mat4::identity();
}
void testGeometryAndPbr(SdlGlRenderDevice& device, RenderItem& item)
{
    item.materialProxy = Proxy(colorDefinition());
    device.beginFrame(view());
    device.drawInstanced({item, item});
    auto hdr = device.ReadHdrAttachmentForTests(0);
    check(hdr[32 * 64 + 32].x > .9f, "Instanced compiled material survives compute culling and rebinding");
    auto weighted = std::make_shared<MeshResource>();
    weighted->load(root + "weighted.mesh");
    auto mesh = item.mesh;
    item.mesh = weighted;
    item.skinMatrices = {translationMatrix({.7f, 0, 0})};
    draw(device, item, view());
    hdr = device.ReadHdrAttachmentForTests(0);
    check(hdr[32 * 64 + 10].x < .02f && hdr[32 * 64 + 50].x > .9f, "Skinned Base uses current bone transform");
    auto two = std::make_shared<MaterialRenderProxy>(*item.materialProxy);
    two->state.twoSided = true;
    item.materialProxy = two;
    device.beginShadowPass(view(), 0);
    device.drawShadow(item);
    check(device.readShadowDepthForTests(320, 1024) > .99f && device.readShadowDepthForTests(1600, 1024) < .6f,
          "Skinned Shadow uses the same current bone transform");
    device.endShadowPass();
    item.mesh = mesh;
    item.skinMatrices.clear();
    item.material = std::make_shared<MaterialResource>();
    item.material->load(root + "pbr.mat");
    auto viewValue = view();
    viewValue.lights[0].intensity = 3;
    viewValue.lights[0].direction = {0, 0, -1};
    item.materialProxy.reset();
    auto old = draw(device, item, viewValue);
    auto oldAmbient = device.ReadHdrAttachmentForTests(2)[32 * 64 + 32];
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    item.materialProxy = system.BuildProxy("assets/pbr.mat");
    auto unified = draw(device, item, viewValue);
    auto ambient = device.ReadHdrAttachmentForTests(2)[32 * 64 + 32];
    const auto relative = [](float a, float b) { return std::abs(a - b) / std::max(.01f, std::abs(a)); };
    check(relative(old.x, unified.x) < .01f && relative(old.y, unified.y) < .01f && relative(old.z, unified.z) < .01f &&
              relative(oldAmbient.x, ambient.x) < .01f,
          "fixed PBR and unified PBR match linear HDR within one percent");
    auto graph = colorDefinition();
    graph.renderState.shadingModel = MaterialShadingModel::Lit;
    graph.parameters[0].defaultValue = Vec4{.4f, .2f, .1f, 1};
    for (auto port : {1u, 2u, 5u})
    {
        auto id = graph.graph.addNode(MaterialNodeType::Constant, "PBR value");
        auto& node = graph.graph.GetNodes().back();
        node.constantType = port == 5 ? MaterialParameterType::Vec3 : MaterialParameterType::Float;
        node.value = port == 1 ? Vec4{.6f, 0, 0, 0} : port == 2 ? Vec4{.35f, 0, 0, 0} : Vec4{.1f, .05f, .02f, 0};
        graph.graph.connect(id, 1, port);
    }
    item.materialProxy = Proxy(graph);
    const auto graphPixel = draw(device, item, viewValue);
    check(relative(old.x, graphPixel.x) < .01f && relative(old.y, graphPixel.y) < .01f &&
              relative(old.z, graphPixel.z) < .01f,
          "node graph PBR matches fixed PBR HDR within one percent");
    resources.shutdown();
    item.material.reset();
}
void testGpuCandidateRollback(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    graph.graph.GetParameters() = graph.parameters;
    graph.graph.GetRenderState() = graph.renderState;
    PlatformFile::writeAllText(root + "reload.matgraph", graph.graph.serialize());
    PlatformFile::writeAllText(root + "reload.mat", "TINY_MAT 2\ndefinition \"reload.matgraph\"\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    auto active = system.BuildProxy("assets/reload.mat");
    item.materialProxy = active;
    auto red = draw(device, item, view());
    const auto disk = PlatformFile::readAllText(root + "reload.matgraph");
    graph.parameters[0].defaultValue = Vec4{0, 1, 0, 1};
    const auto result = system.applyDefinitionDraft("assets/reload.matgraph", graph,
                                                    [&](const auto& compiled, auto& diagnostics)
                                                    {
                                                        auto invalid = compiled;
                                                        invalid.surfaceSource += "\nthis_is_invalid_glsl;\n";
                                                        return device.validateMaterial(invalid, diagnostics);
                                                    });
    item.materialProxy = system.BuildProxy("assets/reload.mat");
    const auto retained = draw(device, item, view());
    check(!result.success && !result.diagnostics.empty() && item.materialProxy == active &&
              isClose(red.x, retained.x) && PlatformFile::readAllText(root + "reload.matgraph") == disk,
          "real GPU compile failure rolls back disk and complete live snapshot");
    const auto accept = [&](const auto& compiled, auto& diagnostics)
    { return device.validateMaterial(compiled, diagnostics); };
    check(system.applyDefinitionDraft("assets/reload.matgraph", graph, accept).success,
          "fixed GPU candidate publishes all geometry/pass variants");
    const auto programs = device.getMaterialProgramCompileCountForTests(MaterialPass::Base);
    graph.parameters[0].defaultValue = Vec4{0, 0, 1, 1};
    check(system.applyDefinitionDraft("assets/reload.matgraph", graph, accept).success &&
              device.getMaterialProgramCompileCountForTests(MaterialPass::Base) == programs,
          "default-only publication reuses GPU programs");
    item.materialProxy = system.BuildProxy("assets/reload.mat");
    auto blue = draw(device, item, view());
    check(blue.z > .9f, "published child actually renders new parent default");
    auto invalid = std::make_shared<CompiledMaterial>(*item.materialProxy->compiled);
    invalid->surfaceSource += "\ninvalid_first_load;\n";
    auto broken = std::make_shared<MaterialRenderProxy>(*item.materialProxy);
    broken->compiled = invalid;
    item.materialProxy = broken;
    const auto pink = draw(device, item, view());
    check(pink.x > .9f && pink.z > .9f && pink.y < .02f, "first GPU failure renders magenta fallback");
    auto textured = textureDefinition(TextureSampling::Linear);
    textured.parameters[0].defaultValue = std::string("snapshot.ppm");
    textured.graph.GetParameters() = textured.parameters;
    textured.graph.GetRenderState() = textured.renderState;
    PlatformFile::writeAllText(root + "textured.matgraph", textured.graph.serialize());
    PlatformFile::writeAllText(root + "snapshot.ppm", "P3\n1 1\n255\n64 64 64\n");
    item.materialProxy = system.BuildProxy("assets/textured.matgraph");
    const auto textureBefore = item.materialProxy;
    auto dark = draw(device, item, view());
    const auto texturePrograms = device.getMaterialProgramCompileCountForTests(MaterialPass::Base);
    PlatformFile::writeAllText(root + "snapshot.ppm", "P3\n1 1\n255\n192 192 192\n");
    check(system.reload("assets/textured.matgraph", accept).success,
          "reload validates a same-path texture content replacement");
    item.materialProxy = system.BuildProxy("assets/textured.matgraph");
    const auto light = draw(device, item, view());
    check(isClose(dark.x, 64.f / 255) && isClose(light.x, 192.f / 255) &&
              textureBefore->textures[0].version != item.materialProxy->textures[0].version,
          "new texture content publishes independently while old snapshot retains its resource");
    check(device.getMaterialProgramCompileCountForTests(MaterialPass::Base) <= texturePrograms + 2,
          "texture replacement only validates missing geometry variants");
    resources.shutdown();
}
void testGpuGenerationLifetime(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = colorDefinition();
    graph.parameters.clear();
    graph.graph.GetNodes().resize(1);
    graph.graph.GetLinks().clear();
    const auto constant = graph.graph.addNode(MaterialNodeType::Constant, "Color");
    const auto mask = graph.graph.addNode(MaterialNodeType::ComponentMask, "RGB");
    graph.graph.GetNodes().back().channelMask = "xyz";
    graph.graph.connect(constant, mask, 0);
    graph.graph.connect(mask, 1, 0);
    std::vector<GLuint> handles, textureHandles;
    std::shared_ptr<MaterialRenderProxy> retainedProgram, retainedTexture;
    for (int i = 0; i < 24; ++i)
    {
        graph.graph.GetNodes()[1].value.x = .013f * (i + 1);
        auto proxy = Proxy(graph);
        std::vector<MaterialDiagnostic> diagnostics;
        check(device.validateMaterial(*proxy->compiled, diagnostics),
              "validate an edited literal across all six variants");
        item.materialProxy = proxy;
        device.beginFrame(view());
        device.draw(item);
        GLint handle = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &handle);
        handles.push_back(handle);
        device.endFrame();
        if (i == 0)
            retainedProgram = proxy;
    }
    for (int i = 0; i < 24; ++i)
    {
        auto proxy = Proxy(textureDefinition(TextureSampling::Linear));
        auto texture = std::make_shared<TextureResource>();
        texture->load(root + "gray.ppm");
        proxy->textures[0].resource = texture;
        proxy->textures[0].version = 10000 + i;
        item.materialProxy = proxy;
        device.beginFrame(view());
        device.draw(item);
        GLint handle = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &handle);
        textureHandles.push_back(handle);
        device.endFrame();
        if (i == 0)
            retainedTexture = proxy;
    }
    item.materialProxy.reset();
    for (int i = 0; i < 4; ++i)
    {
        device.beginFrame(view());
        device.endFrame();
    }
    const auto isProgram = reinterpret_cast<PFNGLISPROGRAMPROC>(SDL_GL_GetProcAddress("glIsProgram"));
    check(isProgram(handles[0]) && glIsTexture(textureHandles[0]),
          "old GPU objects remain valid while a snapshot still owns them");
    retainedProgram.reset();
    retainedTexture.reset();
    for (int i = 0; i < 4; ++i)
    {
        device.beginFrame(view());
        device.endFrame();
    }
    check(std::count_if(handles.begin(), handles.end(), [&](GLuint id) { return isProgram(id); }) <= 4,
          "repeated edits retire obsolete GL programs after snapshot release");
    check(std::count_if(textureHandles.begin(), textureHandles.end(), [](GLuint id) { return glIsTexture(id); }) <= 1,
          "repeated texture versions retire after snapshot release at frame boundaries");
}
void testLegacyResolvedPasses(SdlGlRenderDevice& device, RenderItem& item)
{
    PlatformFile::writeAllText(root + "compat.frag", "uniform vec2 uClusterDepthRange;\n");
    PlatformFile::writeAllText(root + "compat.mat", "color 0 0 0 1\nemissive 1 0 0\nblend alpha\nshader compat.frag\n");
    PlatformFile::writeAllText(root + "compatChild.mat",
                               "TINY_MAT 2\nparent \"compat.mat\"\nparameter \"Emissive\" Vec3 0 1 0\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    item.material = resources.load<MaterialResource>("assets/compatChild.mat");
    item.materialProxy = system.BuildProxy("assets/compatChild.mat");
    item.transparent = true;
    const auto child = draw(device, item, view());
    check(child.y > .95f && child.x < .02f, "legacy shader branch uses resolved typed child values");
    auto dynamic = system.CreateDynamicInstance("assets/compat.mat");
    dynamic->setParameter("Emissive", Vec3{0, 0, 1});
    item.material = resources.load<MaterialResource>("assets/compat.mat");
    item.materialProxy = system.BuildProxy("assets/compat.mat", dynamic.get());
    const auto blue = draw(device, item, view());
    check(blue.z > .95f && blue.x < .02f, "legacy shader branch uses independent dynamic values");
    device.beginFrame(view());
    device.drawInstanced({item, item});
    const auto instanced = device.ReadHdrAttachmentForTests(0)[32 * 64 + 32];
    check(instanced.z > .95f && instanced.x < .02f, "legacy shader instancing uses effective dynamic snapshot");
    dynamic->setParameter("EmissiveMap", std::string("assets/dark.ppm"));
    item.materialProxy = system.BuildProxy("assets/compat.mat", dynamic.get());
    const auto textured = draw(device, item, view());
    check(isClose(textured.z, .05127f, .008f), "legacy shader uses effective texture overrides with declared sampling");
    item.model = scaleMatrix({-1, 1, 1});
    device.beginShadowPass(view(), 0);
    glDisable(GL_CULL_FACE);
    device.drawShadow(item);
    const auto opaqueShadow = device.readShadowDepthForTests(1024, 1024);
    device.endShadowPass();
    check(opaqueShadow < .6f, "legacy shadow opacity fixture casts before transparent override");
    dynamic->setParameter("BaseColor", Vec4{0, 0, 0, 0});
    item.materialProxy = system.BuildProxy("assets/compat.mat", dynamic.get());
    device.beginShadowPass(view(), 0);
    glDisable(GL_CULL_FACE);
    device.drawShadow(item);
    const auto shadow = device.readShadowDepthForTests(1024, 1024);
    device.endShadowPass();
    device.beginShadowPass(view(), 0);
    glDisable(GL_CULL_FACE);
    device.drawShadowInstanced({item, item});
    const auto instancedShadow = device.readShadowDepthForTests(1024, 1024);
    device.endShadowPass();
    check(shadow > .99f && instancedShadow > .99f,
          "legacy shadow paths consume effective opacity from dynamic snapshot");
    item.model = Mat4::identity();
    PlatformFile::writeAllText(root + "compat.mat",
                               "color 0 0 0 1\nshader compat.frag\nparameter uClusterDepthRange .2 .7 0 0\n");
    system.reload("assets/compat.mat");
    item.materialProxy = system.BuildProxy("assets/compatChild.mat");
    item.material = resources.load<MaterialResource>("assets/compatChild.mat");
    item.transparent = false;
    draw(device, item, view());
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const auto location = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(SDL_GL_GetProcAddress("glGetUniformLocation"));
    const auto read = reinterpret_cast<PFNGLGETUNIFORMFVPROC>(SDL_GL_GetProcAddress("glGetUniformfv"));
    float range[2]{};
    const auto uniform = location(program, "uClusterDepthRange");
    if (uniform >= 0)
        read(program, uniform, range);
    check(uniform >= 0 && isClose(range[0], .2f) && isClose(range[1], .7f),
          "typed legacy child preserves reflected custom uniform behavior");
    item.materialProxy.reset();
    resources.shutdown();
}
void testPackagedWorkingDirectory(SdlGlRenderDevice& device, RenderItem& item)
{
    auto graph = textureDefinition(TextureSampling::Linear);
    graph.parameters[0].defaultValue = std::string("gray.ppm");
    graph.graph.GetParameters() = graph.parameters;
    graph.graph.GetRenderState() = graph.renderState;
    PlatformFile::writeAllText(root + "Package/assets/Parent.matgraph", graph.graph.serialize());
    PlatformFile::writeAllText(root + "Package/assets/Base.mat", "TINY_MAT 2\ndefinition \"Parent.matgraph\"\n");
    PlatformFile::writeAllText(root + "Package/assets/Child.mat", "TINY_MAT 2\nparent \"Base.mat\"\n");
    PlatformFile::writeAllText(root + "Package/assets/gray.ppm", "P3\n1 1\n255\n128 128 128\n");
    const auto archive = std::filesystem::absolute(root + "Game.tpak");
    check(PakArchive::packDirectory(root + "Package", archive), "pack parent graph, instance chain and texture source");
    struct Restore
    {
        std::filesystem::path cwd = std::filesystem::current_path();
        ~Restore()
        {
            PlatformFile::clearMounts();
            std::filesystem::current_path(cwd);
        }
    } restore;
    auto foreign = std::filesystem::absolute(root + "ForeignCwd");
    std::filesystem::create_directories(foreign);
    std::filesystem::current_path(foreign);
    PlatformFile::clearMounts();
    check(PlatformFile::mountPak(archive), "mount material package from a different working directory");
    PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    ResourceManager resources;
    resources.initialize(nullptr, "assets");
    MaterialSystem system(resources);
    item.materialProxy = system.BuildProxy("assets/Child.mat");
    auto pixel = draw(device, item, view());
    check(system.resolve("assets/Child.mat") && isClose(pixel.x, 128.f / 255.f),
          "mounted graph, inherited instance and default texture render correctly without loose files");
    resources.shutdown();
}
} // namespace
int main()
{
    std::cout.setf(std::ios::unitbuf);
    try
    {
        PlatformFile::writeAllText(
            root + "quad.mesh", "v -0.8 -0.8 0\nv 0.8 -0.8 0\nv 0.8 0.8 0\nv -0.8 0.8 0\n"
                                "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        PlatformFile::writeAllText(root + "gray.ppm", "P3\n1 1\n255\n128 128 128\n");
        PlatformFile::writeAllText(root + "dark.ppm", "P3\n1 1\n255\n64 64 64\n");
        auto weightedSource = PlatformFile::readAllText(root + "quad.mesh");
        weightedSource.insert(
            weightedSource.find("f 1/"),
            "vw 1 0 1 0 0 0 0 0 0\nvw 2 0 1 0 0 0 0 0 0\nvw 3 0 1 0 0 0 0 0 0\nvw 4 0 1 0 0 0 0 0 0\n");
        PlatformFile::writeAllText(root + "weighted.mesh", weightedSource);
        PlatformFile::writeAllText(root + "pbr.mat",
                                   "color .4 .2 .1 1\nmetallic .6\nroughness .35\nemissive .1 .05 .02\n");
        SdlGlRenderDevice device(64, 64, "Material GPU tests");
        check(device.initialize(), "initialize real OpenGL 4.3 context");
        if (failures)
            return 1;
        SDL_HideWindow(static_cast<SDL_Window*>(device.getNativeWindow()));
        std::cout << "GPU: " << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n';
        device.enableSceneRenderTarget(true, 64, 64);
        device.setTaaEnabled(false);
        device.setBloomEnabled(false);
        device.setGtaoEnabled(false);
        device.setFxaaEnabled(false);
        RenderItem item;
        item.mesh = std::make_shared<MeshResource>();
        check(item.mesh->load(root + "quad.mesh"), "load GPU test quad");
        testViewportModes(device, item);
        testSharedPrograms(device, item);
        testParameterUpload(device, item);
        testTextureSemantics(device, item);
        testFallbackTexture(device, item);
        testTextureBudget(device, item);
        testPassClippingAndState(device, item);
        testGeometryAndPbr(device, item);
        testGpuCandidateRollback(device, item);
        testGpuGenerationLifetime(device, item);
        testLegacyResolvedPasses(device, item);
        testPackagedWorkingDirectory(device, item);
        device.shutdown();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
