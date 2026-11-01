#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/render/sdl_vulkan_render_device.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/vfx/particle_system.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace tinyengine;
namespace
{
constexpr int size = 64;
const std::string root = "saved/VulkanRender/";
int failures = 0;
bool failLineBuffer = false;
void check(bool ok, const char* name)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += !ok;
}
bool close(float a, float b, float tolerance = .008f)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < tolerance;
}
Vec4 pixel(const std::vector<Vec4>& pixels, int x = 32, int y = 32, int width = size)
{
    return pixels.at(std::size_t(y) * width + x);
}
std::shared_ptr<MaterialRenderProxy> proxy(const MaterialDefinitionData& definition)
{
    const auto compiled = MaterialCompiler{}.compile(definition);
    if (!compiled.Success())
        throw std::runtime_error("CPU material fixture did not compile");
    auto result = std::make_shared<MaterialRenderProxy>();
    result->compiled = compiled.material;
    result->state = definition.renderState;
    for (const auto& slot : result->compiled->layout.numericSlots)
    {
        const auto p = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                    [&](const auto& p) { return p.name == slot.name; });
        if (p == definition.parameters.end())
            throw std::runtime_error("Fixture numeric slot has no parameter");
        result->numericSlots.push_back(materialNumericSlot(p->defaultValue));
    }
    for (const auto& slot : result->compiled->layout.textureSlots)
        result->textures.push_back({slot.name, slot.slot, {}, slot.sampling});
    return result;
}
void value(MaterialRenderProxy& p, const std::string& name, Vec4 v)
{
    for (std::size_t i = 0; i < p.compiled->layout.numericSlots.size(); ++i)
        if (p.compiled->layout.numericSlots[i].name == name)
        {
            p.numericSlots[i] = v;
            return;
        }
    throw std::runtime_error("Fixture parameter is absent: " + name);
}
std::shared_ptr<MaterialRenderProxy> material(Vec4 color = {.6f, .2f, .1f, 1},
                                              MaterialShadingModel shading = MaterialShadingModel::Unlit,
                                              MaterialBlendMode blend = MaterialBlendMode::Opaque,
                                              bool twoSided = false)
{
    auto definition = makeBuiltinPbrDefinition();
    definition.renderState.shadingModel = shading;
    definition.renderState.blendMode = blend;
    definition.renderState.twoSided = twoSided;
    auto result = proxy(definition);
    value(*result, "BaseColor", color);
    return result;
}
MaterialDefinitionData textureDefinition(TextureSampling sampling)
{
    MaterialDefinitionData d;
    d.sourcePath = "gpu:vulkan-texture";
    d.graph.setFormatVersion(3);
    d.renderState.shadingModel = MaterialShadingModel::Unlit;
    d.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    MaterialParameterDecl p;
    p.name = "Map";
    p.type = MaterialParameterType::Texture2D;
    p.defaultValue = std::string{};
    p.sampling = sampling;
    d.parameters.push_back(p);
    const auto handle = d.graph.addNode(MaterialNodeType::TextureParameter, "Map");
    d.graph.GetNodes().back().parameterName = "Map";
    const auto sample = d.graph.addNode(MaterialNodeType::TextureSample, "Sample");
    d.graph.connect(handle, sample, 0);
    const auto rgb = d.graph.addNode(MaterialNodeType::ComponentMask, "RGB");
    d.graph.GetNodes().back().channelMask = "xyz";
    d.graph.connect(sample, rgb, 0);
    d.graph.connect(rgb, 1, 0);
    return d;
}
std::shared_ptr<MaterialRenderProxy> textured(const std::string& path, TextureSampling sampling)
{
    auto result = proxy(textureDefinition(sampling));
    auto texture = std::make_shared<TextureResource>();
    if (!texture->load(path))
        throw std::runtime_error("Cannot load texture fixture: " + path);
    result->textures.at(0).resource = texture;
    return result;
}
FrameView view(bool lit = false)
{
    FrameView v;
    v.cameraPosition = {0, 0, 3};
    v.view = lookAt(v.cameraPosition, {}, {0, 1, 0});
    v.projection = perspective(60, 1, .1f, 100);
    v.previousViewProjection = v.projection * v.view;
    for (auto& matrix : v.shadowViewProjections)
        matrix = Mat4::identity();
    v.shadowCascadeSplits = {1, 10, 100};
    v.clusteredLights.tilesX = v.clusteredLights.tilesY = v.clusteredLights.slicesZ = 1;
    v.clusteredLights.nearPlane = .1f;
    v.clusteredLights.farPlane = 100;
    v.clusteredLights.offsets = {0};
    v.clusteredLights.counts = {lit ? 1u : 0u};
    if (lit)
    {
        v.lightCount = 1;
        v.lights[0].type = RenderLightType::Point;
        v.lights[0].position = {0, 0, 2};
        v.lights[0].intensity = 4;
        v.clusteredLights.lightIndices = {0};
    }
    return v;
}
template <class Device> void configure(Device& device)
{
    SDL_HideWindow(static_cast<SDL_Window*>(device.getNativeWindow()));
    check(device.enableSceneRenderTarget(true, size, size), "create actual offscreen scene target");
    device.setTaaEnabled(false);
    device.setFxaaEnabled(false);
    device.setBloomEnabled(false);
    device.setGtaoEnabled(false);
}
void finish(SdlVulkanRenderDevice& device)
{
    device.endFrame();
    device.present();
}
std::vector<Vec4> render(SdlVulkanRenderDevice& device, const RenderItem& item, const FrameView& v = view(),
                         bool deferred = false)
{
    device.beginFrame(v);
    if (deferred)
        device.beginGBufferPass();
    device.draw(item);
    if (deferred)
    {
        device.endGBufferPass();
        device.executeDeferredLighting();
    }
    auto pixels = device.readHdrAttachment();
    finish(device);
    return pixels;
}
void writeU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t number)
{
    for (int i = 0; i < 4; ++i)
        data.at(offset + i) = std::uint8_t(number >> (i * 8));
}
void writeDds(const std::string& name, std::uint32_t fourCc, const std::vector<std::uint8_t>& block,
              std::uint32_t mipCount = 1)
{
    std::vector<std::uint8_t> bytes(128 + block.size(), 0);
    writeU32(bytes, 0, 0x20534444);
    writeU32(bytes, 4, 124);
    writeU32(bytes, 8, 0x81007);
    writeU32(bytes, 12, 4);
    writeU32(bytes, 16, 4);
    writeU32(bytes, 20, std::uint32_t(block.size()));
    writeU32(bytes, 28, mipCount);
    writeU32(bytes, 76, 32);
    writeU32(bytes, 80, 4);
    writeU32(bytes, 84, fourCc);
    writeU32(bytes, 108, 0x1000);
    std::copy(block.begin(), block.end(), bytes.begin() + 128);
    if (!PlatformFile::writeAllBytes(root + name, bytes))
        throw std::runtime_error("Cannot write DDS fixture");
}
RenderItem fixtures()
{
    const std::string quad = "TINY_MESH 3\nv -.8 -.8 0\nv .8 -.8 0\nv .8 .8 0\nv -.8 .8 0\n"
                             "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                             "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";
    PlatformFile::writeAllText(root + "quad.mesh", quad);
    auto weighted = quad;
    // Actual two-bone blend: .25 * (-.6) + .75 * 1.0 translates every vertex by .6.
    weighted.insert(weighted.find("f 1/"), "vw 1 0 .25 1 .75 0 0 0 0\nvw 2 0 .25 1 .75 0 0 0 0\n"
                                           "vw 3 0 .25 1 .75 0 0 0 0\nvw 4 0 .25 1 .75 0 0 0 0\n");
    PlatformFile::writeAllText(root + "weighted.mesh", weighted);
    PlatformFile::writeAllText(root + "gray.ppm", "P3\n1 1\n255\n128 128 128\n");
    PlatformFile::writeAllText(root + "quadrants.ppm", "P3\n2 2\n255\n255 0 0  0 255 0\n0 0 255  255 255 0\n");
    writeDds("red-bc1.dds", 0x31545844, {0, 248, 0, 248, 0, 0, 0, 0});
    writeDds("blue-bc3.dds", 0x35545844, {255, 255, 0, 0, 0, 0, 0, 0, 31, 0, 31, 0, 0, 0, 0, 0});
    writeDds("rg-bc5.dds", 0x32495441, {192, 192, 0, 0, 0, 0, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0});
    writeDds("mips-bc1.dds", 0x31545844,
             {0, 248, 0, 248, 0, 0, 0, 0, 224, 7, 224, 7, 0, 0, 0, 0, 31, 0, 31, 0, 0, 0, 0, 0}, 3);
    RenderItem item;
    item.mesh = std::make_shared<MeshResource>();
    if (!item.mesh->load(root + "quad.mesh"))
        throw std::runtime_error("Cannot load GPU quad fixture");
    item.worldBounds = item.mesh->GetBounds();
    item.materialProxy = material();
    return item;
}
void saveHdr(const std::string& name, const std::vector<Vec4>& image)
{
    const std::string header = "P6\n64 64\n255\n";
    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    for (int y = size - 1; y >= 0; --y)
        for (int x = 0; x < size; ++x)
        {
            const auto p = pixel(image, x, y);
            for (const float channel : {p.x, p.y, p.z})
                bytes.push_back(std::uint8_t(std::clamp(channel, 0.f, 1.f) * 255.f + .5f));
        }
    PlatformFile::writeAllBytes(root + name, bytes);
}
void saveFloatPixels(const std::string& name, const std::vector<Vec4>& image)
{
    std::vector<std::uint8_t> bytes(image.size() * sizeof(Vec4));
    std::memcpy(bytes.data(), image.data(), bytes.size());
    PlatformFile::writeAllBytes(root + name, bytes);
}
void diagnoseDifference(const char* label, const std::vector<Vec4>& actual, const std::vector<Vec4>& reference)
{
    std::size_t worst = 0;
    float maximum = -1;
    for (std::size_t i = 0; i < std::min(actual.size(), reference.size()); ++i)
    {
        const float delta = std::max({std::abs(actual[i].x - reference[i].x), std::abs(actual[i].y - reference[i].y),
                                      std::abs(actual[i].z - reference[i].z)});
        if (delta > maximum)
        {
            maximum = delta;
            worst = i;
        }
    }
    const auto a = actual.at(worst), b = reference.at(worst), center = pixel(actual), glCenter = pixel(reference);
    std::cout << label << " max at (" << worst % size << ',' << worst / size << ") Vulkan=(" << a.x << ',' << a.y << ','
              << a.z << ") OpenGL=(" << b.x << ',' << b.y << ',' << b.z << ") center Vulkan=(" << center.x << ','
              << center.y << ',' << center.z << ") OpenGL=(" << glCenter.x << ',' << glCenter.y << ',' << glCenter.z
              << ")\n";
}
float difference(const std::vector<Vec4>& a, const std::vector<Vec4>& b)
{
    if (a.size() != b.size())
        return 1000000;
    float error = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        for (float delta : {a[i].x - b[i].x, a[i].y - b[i].y, a[i].z - b[i].z})
            if (!std::isfinite(delta))
                return 1000000;
            else
                error = std::max(error, std::abs(delta));
    return error;
}
struct GlReference
{
    std::vector<Vec4> forward, deferred, quadrants;
    std::vector<float> depth;
};
std::shared_ptr<MaterialRenderProxy> litMaterial()
{
    auto p = material({.4f, .2f, .1f, 1}, MaterialShadingModel::Lit);
    value(*p, "Metallic", {.6f, 0, 0, 0});
    value(*p, "Roughness", {.35f, 0, 0, 0});
    value(*p, "Emissive", {.1f, .05f, .02f, 0});
    return p;
}
GlReference openGlReference(RenderItem item)
{
    SdlGlRenderDevice device(size, size, "OpenGL Vulkan parity reference");
    if (!device.initialize())
        throw std::runtime_error("Actual OpenGL reference context is unavailable");
    configure(device);
    GlReference result;
    item.materialProxy = litMaterial();
    device.beginFrame(view(true));
    device.draw(item);
    result.depth.resize(size * size);
    glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, result.depth.data());
    result.forward = device.ReadHdrAttachmentForTests();
    device.endFrame();
    device.present();
    device.beginFrame(view(true));
    device.beginGBufferPass();
    device.draw(item);
    device.endGBufferPass();
    device.executeDeferredLighting();
    result.deferred = device.ReadHdrAttachmentForTests();
    device.endFrame();
    device.present();
    item.materialProxy = textured(root + "quadrants.ppm", TextureSampling::Linear);
    device.beginFrame(view());
    device.draw(item);
    result.quadrants = device.ReadHdrAttachmentForTests();
    check(glGetError() == GL_NO_ERROR, "OpenGL reference scene has no API errors");
    device.endFrame();
    device.present();
    device.shutdown();
    saveHdr("opengl-lit.ppm", result.forward);
    return result;
}
void testLighting(SdlVulkanRenderDevice& device, RenderItem item, const GlReference& reference)
{
    item.materialProxy = litMaterial();
    device.beginFrame(view(true));
    device.draw(item);
    const auto forward = device.readHdrAttachment();
    const auto depth = device.readDepthPixels();
    check(close(depth.at(32 * size + 32), reference.depth.at(32 * size + 32), .0001f) && close(depth.at(0), 1),
          "Vulkan scene depth agrees with OpenGL clip conversion and clear depth");
    finish(device);
    device.beginFrame(view(true));
    device.beginGBufferPass();
    device.draw(item);
    device.endGBufferPass();
    check(close(pixel(device.readHdrAttachment()).x, 0), "G-buffer does not shade HDR before lighting");
    device.executeDeferredLighting();
    const auto deferred = device.readHdrAttachment();
    const auto ambient = device.readHdrAttachment(2);
    check(pixel(forward).x > .1f && difference(forward, deferred) < .015f,
          "independent Vulkan deferred PBR agrees with Vulkan forward lighting");
    const auto fError = difference(forward, reference.forward), dError = difference(deferred, reference.deferred);
    std::cout << "HDR maximum RGB error: forward=" << fError << ", deferred=" << dError << '\n';
    diagnoseDifference("Forward", forward, reference.forward);
    diagnoseDifference("Deferred", deferred, reference.deferred);
    check(fError < .02f && dError < .02f, "lit Vulkan forward and deferred pixels agree with real OpenGL");
    check(std::isfinite(pixel(ambient).x) && pixel(ambient).x >= 0,
          "deferred ambient output is finite and nonnegative");
    finish(device);
    saveHdr("vulkan-forward-lit.ppm", forward);
    saveHdr("vulkan-deferred-lit.ppm", deferred);
    saveFloatPixels("opengl-forward-rgba.f32", reference.forward);
    saveFloatPixels("opengl-deferred-rgba.f32", reference.deferred);
    saveFloatPixels("vulkan-forward-rgba.f32", forward);
    saveFloatPixels("vulkan-deferred-rgba.f32", deferred);
    std::vector<Vec4> diff(forward.size());
    for (std::size_t i = 0; i < diff.size(); ++i)
        diff[i] = {std::abs(forward[i].x - reference.forward[i].x) * 16,
                   std::abs(forward[i].y - reference.forward[i].y) * 16,
                   std::abs(forward[i].z - reference.forward[i].z) * 16, 1};
    saveHdr("vulkan-opengl-difference-x16.ppm", diff);
    auto dark = view(true);
    dark.lights[0].intensity = 0;
    check(pixel(forward).x > pixel(render(device, item, dark)).x + .01f,
          "PBR lighting responds to actual clustered point-light intensity");
}
void testResourceFallbacks(SdlVulkanRenderDevice& device, RenderItem item)
{
    auto pending = std::make_shared<TextureResource>();
    pending->markLoading("async:environment");
    item.materialProxy = litMaterial();
    const auto baseline = render(device, item, view(true));
    auto v = view(true);
    v.environmentIrradiance = v.environmentPrefilter = v.environmentBrdfLut = pending;
    check(difference(render(device, item, v), baseline) < .002f,
          "unready IBL resources preserve the no-environment lighting fallback");
    v.environmentSky = pending;
    device.beginFrame(v);
    device.drawSkybox();
    check(close(pixel(device.readHdrAttachment()).x, 0), "unready sky texture preserves the scene background");
    finish(device);
    const auto late = root + "late-ui.ppm";
    std::error_code ec;
    std::filesystem::remove(late, ec);
    UIDrawCommand background{UIWidgetType::Panel, {0, 0, size, size}, {0, 0, 0, 1}};
    UIDrawCommand image{UIWidgetType::Image, {8, 8, 16, 16}, {1, 1, 1, 1}};
    image.imagePath = late;
    device.beginFrame(view());
    device.drawUi({background, image});
    finish(device);
    PlatformFile::writeAllText(late, "P3\n1 1\n255\n128 128 128\n");
    device.beginFrame(view());
    device.drawUi({background, image});
    const auto pixels = device.readSceneColorPixels();
    check(std::abs(int(pixels[((size - 16) * size + 16) * 4]) - 55) <= 2,
          "UI retries a previously missing image after it becomes available");
    finish(device);
}
void testGeometry(SdlVulkanRenderDevice& device, RenderItem item)
{
    item.materialProxy = material({.7f, .1f, .2f, 1});
    item.mesh = std::make_shared<MeshResource>();
    check(item.mesh->load(root + "weighted.mesh"), "load two-bone weighted mesh fixture");
    item.skinMatrices = {translationMatrix({-.6f, 0, 0}), translationMatrix({1, 0, 0})};
    for (bool deferred : {false, true})
    {
        device.beginFrame(view());
        if (deferred)
            device.beginGBufferPass();
        device.draw(item);
        if (deferred)
        {
            device.endGBufferPass();
            device.executeDeferredLighting();
        }
        const auto pixels = device.readHdrAttachment();
        const auto depth = device.readDepthPixels();
        check(close(pixel(pixels, 20).x, 0) && close(pixel(pixels, 50).x, .7f) && close(depth.at(32 * size + 20), 1) &&
                  depth.at(32 * size + 50) < .99f,
              deferred ? "weighted skin blend moves deferred color and depth coverage"
                       : "weighted skin blend moves forward color and depth coverage");
        finish(device);
    }
    item.skinMatrices = {Mat4::identity(), Mat4::identity()};
    const auto stationary = render(device, item);
    check(close(pixel(stationary, 20).x, .7f) && close(pixel(stationary, 50).x, 0),
          "changing bone matrices restores original weighted-mesh coverage");
    item.mesh = std::make_shared<MeshResource>();
    item.mesh->load(root + "quad.mesh");
    item.skinMatrices.clear();
    RenderItem left = item, right = item;
    left.model = transformMatrix({-.9f, 0, 0}, {}, {.4f, .4f, 1});
    right.model = transformMatrix({.9f, 0, 0}, {}, {.65f, .65f, 1});
    left.previousModel = left.model;
    right.previousModel = right.model;
    left.worldBounds = {{-1.22f, -.32f, 0}, {-.58f, .32f, 0}};
    right.worldBounds = {{.38f, -.52f, 0}, {1.42f, .52f, 0}};
    for (bool deferred : {false, true})
    {
        device.beginFrame(view());
        if (deferred)
            device.beginGBufferPass();
        device.drawInstanced({left, right});
        if (deferred)
        {
            device.endGBufferPass();
            device.executeDeferredLighting();
        }
        const auto pixels = device.readHdrAttachment();
        check(close(pixel(pixels, 15).x, .7f) && close(pixel(pixels, 49).x, .7f) && close(pixel(pixels, 32).x, 0) &&
                  close(pixel(pixels, 15, 40).x, 0) && close(pixel(pixels, 49, 40).x, .7f),
              deferred ? "deferred instances retain distinct translation and scale after GPU culling"
                       : "forward instances retain distinct translation and scale after GPU culling");
        finish(device);
    }
}
void testSurfaceState(SdlVulkanRenderDevice& device, RenderItem item)
{
    auto d = textureDefinition(TextureSampling::Linear);
    d.renderState.blendMode = MaterialBlendMode::Masked;
    d.renderState.twoSided = true;
    const auto uv = d.graph.addNode(MaterialNodeType::TexCoord, "UV");
    const auto x = d.graph.addNode(MaterialNodeType::ComponentMask, "Opacity X");
    d.graph.GetNodes().back().channelMask = "x";
    d.graph.connect(uv, x, 0);
    d.graph.connect(x, 1, 6);
    item.materialProxy = proxy(d); // An unbound Map samples opaque white.
    for (bool deferred : {false, true})
    {
        const auto pixels = render(device, item, view(), deferred);
        check(close(pixel(pixels, 22).x, 0) && close(pixel(pixels, 42).x, 1),
              deferred ? "masked deferred surface discards UV holes" : "masked forward surface discards UV holes");
    }
    auto missing = material({1, 1, 1, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Masked);
    value(*missing, "Opacity", {});
    item.materialProxy = missing;
    device.beginFrame(view());
    device.beginGBufferPass();
    device.draw(item);
    device.endGBufferPass();
    device.executeDeferredLighting();
    check(close(pixel(device.readHdrAttachment()).x, 0) && close(device.readDepthPixels().at(32 * size + 32), 1),
          "empty masked frame clears old G-buffer color and preserves far depth");
    finish(device);
    item.model = scaleMatrix({-1, 1, 1});
    item.materialProxy = material({.8f, .2f, .1f, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Opaque, true);
    const auto twoSided = pixel(render(device, item));
    item.materialProxy = material({.8f, .2f, .1f, 1});
    const auto culled = pixel(render(device, item));
    item.materialProxy = material({.8f, .2f, .1f, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Opaque, true);
    check(close(twoSided.x, .8f) && close(culled.x, 0) && close(pixel(render(device, item)).x, .8f),
          "two-sided pipelines render reversed winding and restore culling state");
    item.model = Mat4::identity();
    item.materialProxy = material({1, 0, 0, 1});
    RenderItem green = item, blue = item;
    auto gp = material({0, 1, 0, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Alpha);
    auto bp = material({0, 0, 1, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Alpha);
    value(*gp, "Opacity", {.5f, 0, 0, 0});
    value(*bp, "Opacity", {.5f, 0, 0, 0});
    green.materialProxy = gp;
    blue.materialProxy = bp;
    green.model = translationMatrix({0, 0, .1f});
    blue.model = translationMatrix({0, 0, .2f});
    for (bool deferred : {false, true})
    {
        device.beginFrame(view());
        if (deferred)
            device.beginGBufferPass();
        device.draw(item);
        if (deferred)
        {
            device.endGBufferPass();
            device.executeDeferredLighting();
        }
        const auto before = device.readDepthPixels();
        device.draw(green);
        device.draw(blue);
        const auto mixed = pixel(device.readHdrAttachment());
        check(close(mixed.x, .25f) && close(mixed.y, .25f) && close(mixed.z, .5f) &&
                  close(device.readDepthPixels().at(32 * size + 32), before.at(32 * size + 32), .00001f),
              deferred ? "alpha layers compose once over deferred opaque without writing depth"
                       : "alpha layers compose far-to-near without writing depth");
        finish(device);
    }
}
void testOcclusionHistory(SdlVulkanRenderDevice& device, RenderItem item)
{
    item.model = transformMatrix({0, 0, 1}, {}, {4, 4, 1});
    device.beginFrame(view());
    device.draw(item);
    device.executePostProcess(PostProcessPass::HierarchicalDepth);
    finish(device);
    RenderItem left = item, right = item;
    left.model = transformMatrix({-.4f, 0, -1}, {}, {.2f, .2f, 1});
    right.model = transformMatrix({.4f, 0, -1}, {}, {.2f, .2f, 1});
    left.previousModel = left.model;
    right.previousModel = right.model;
    left.worldBounds = {{-.56f, -.16f, -1}, {-.24f, .16f, -1}};
    right.worldBounds = {{.24f, -.16f, -1}, {.56f, .16f, -1}};
    auto history = view();
    history.resetTemporalHistory = false;
    history.occlusionHistoryValid = true;
    // There is deliberately no current-frame occluder: empty pixels prove that the old HZB culled instances.
    device.beginFrame(history);
    device.drawInstanced({left, right});
    const auto hidden = device.readHdrAttachment();
    check(close(pixel(hidden, 26).x, 0) && close(pixel(hidden, 37).x, 0),
          "previous-frame HZB occludes stationary instances through compute and indirect draw");
    finish(device);
    left.previousModel.at(0, 3) -= .2f;
    device.beginFrame(history);
    device.drawInstanced({left, right});
    const auto moved = device.readHdrAttachment();
    check(close(pixel(moved, 26).x, .6f) && close(pixel(moved, 37).x, 0),
          "moving instance bypasses stale HZB while stationary instance remains culled");
    finish(device);
    history.resetTemporalHistory = true;
    device.beginFrame(history);
    device.drawInstanced({left, right});
    const auto reset = device.readHdrAttachment();
    check(close(pixel(reset, 26).x, .6f) && close(pixel(reset, 37).x, .6f),
          "temporal-history reset restores both instances despite old occlusion depths");
    finish(device);
}
void testTextures(SdlVulkanRenderDevice& device, RenderItem item, const GlReference& reference)
{
    item.materialProxy = textured(root + "gray.ppm", TextureSampling::Linear);
    const auto linear = pixel(render(device, item));
    item.materialProxy = textured(root + "gray.ppm", TextureSampling::SRgb);
    const auto srgb = pixel(render(device, item));
    check(close(linear.x, 128.f / 255) && close(srgb.x, .21586f) && linear.x > srgb.x * 2,
          "same texture has distinct linear and sRGB GPU sampling without double gamma");
    item.materialProxy = textured(root + "quadrants.ppm", TextureSampling::Linear);
    const auto quadrants = render(device, item);
    check(pixel(quadrants, 24, 24).x > .9f && pixel(quadrants, 24, 24).z < .05f && pixel(quadrants, 24, 40).z > .9f &&
              difference(quadrants, reference.quadrants) < .012f,
          "asymmetric texture UV orientation and filtering agree with OpenGL");
    const std::array<std::string, 3> paths{"red-bc1.dds", "blue-bc3.dds", "rg-bc5.dds"};
    const std::array<Vec4, 3> expected{{{1, 0, 0, 1}, {0, 0, 1, 1}, {192.f / 255, 64.f / 255, 0, 1}}};
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        item.materialProxy = textured(root + paths[i], TextureSampling::Linear);
        const auto p = pixel(render(device, item));
        check(close(p.x, expected[i].x) && close(p.y, expected[i].y) && close(p.z, expected[i].z),
              i == 0   ? "BC1 compressed block uploads and samples red"
              : i == 1 ? "BC3 compressed block uploads and samples blue"
                       : "BC5 block retains independent red and green channels");
    }
    auto lod = textured(root + "mips-bc1.dds", TextureSampling::Linear);
    auto compiled = std::make_shared<CompiledMaterial>(*lod->compiled);
    compiled->surfaceSource = "SurfaceData EvaluateSurface(vec2 uv){SurfaceData "
                              "s;s.BaseColor=textureLod(te_texture_0,uv,2.0).rgb;s.Metallic=0;s.Roughness=1;s.NormalTS="
                              "vec3(0,0,1);s.AO=1;s.Emissive=vec3(0);s.Opacity=1;return s;}\n";
    compiled->canonicalProgramContent += "mip-level-2";
    lod->compiled = compiled;
    item.materialProxy = lod;
    const auto mip = pixel(render(device, item));
    check(mip.z > .99f && mip.x < .01f && mip.y < .01f,
          "DDS material samples the supplied second mip instead of clamping to its red base level");
}
void testParticles(SdlVulkanRenderDevice& device, RenderItem item)
{
    Particle cpu;
    cpu.position = {0, 0, .2f};
    cpu.size = .4f;
    cpu.color = {.2f, .8f, .4f, .5f};
    device.beginFrame(view());
    device.drawParticles({cpu});
    const auto p = pixel(device.readHdrAttachment());
    check(close(p.x, .1f) && close(p.y, .4f) && close(p.z, .2f) &&
              close(device.readDepthPixels().at(32 * size + 32), 1),
          "CPU particle billboard blends expected HDR color without writing depth");
    finish(device);
    cpu.texturePath = root + "gray.ppm";
    device.beginFrame(view());
    device.drawParticles({cpu});
    const auto texturedCpu = pixel(device.readHdrAttachment());
    check(close(texturedCpu.x, .1f * .21586f) && close(texturedCpu.y, .4f * .21586f),
          "CPU sprite colors decode sRGB before HDR particle blending");
    finish(device);
    cpu.texturePath.clear();
    item.materialProxy = material({.7f, .1f, .2f, 1});
    cpu.position.z = -.2f;
    device.beginFrame(view());
    device.draw(item);
    device.drawParticles({cpu});
    check(close(pixel(device.readHdrAttachment()).x, .7f), "opaque scene depth occludes a CPU particle behind it");
    finish(device);
    GpuParticleEmitter emitter;
    emitter.id = 7;
    emitter.settings.maximumParticles = 1;
    emitter.settings.emissionRate = 1;
    emitter.settings.lifetime = 2;
    emitter.settings.speed = 1;
    emitter.settings.spread = 0;
    emitter.settings.gravity = {};
    emitter.settings.startSize = emitter.settings.endSize = .2f;
    emitter.settings.startColor = emitter.settings.endColor = {.1f, .3f, .9f, 1};
    device.beginFrame(view());
    device.drawGpuParticles({emitter});
    check(close(pixel(device.readHdrAttachment()).z, .9f),
          "GPU emitter generates its first visible billboard on the GPU");
    finish(device);
    emitter.settings.texturePath = root + "gray.ppm";
    device.beginFrame(view());
    device.drawGpuParticles({emitter});
    check(close(pixel(device.readHdrAttachment()).z, .9f * .21586f),
          "GPU sprite colors decode sRGB before HDR particle blending");
    finish(device);
    emitter.settings.texturePath.clear();
    emitter.elapsedTime = .6f;
    device.beginFrame(view());
    device.drawGpuParticles({emitter});
    const auto moved = device.readHdrAttachment();
    check(close(pixel(moved).z, 0) && pixel(moved, 32, 43).z > .85f,
          "GPU emitter time advances particle position instead of drawing a static proxy");
    finish(device);
    emitter.settings.speed = 0;
    emitter.settings.lifetime = .2f;
    device.beginFrame(view());
    device.drawGpuParticles({emitter});
    check(close(pixel(device.readHdrAttachment()).z, 0), "GPU particles outside their lifetime discard all fragments");
    finish(device);
}
void testShadows(SdlVulkanRenderDevice& device, RenderItem item)
{
    auto v = view();
    item.materialProxy = material({1, 0, 0, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Opaque, true);
    // The render graph starts shadow recording before beginFrame.
    device.beginShadowPass(v, 0);
    device.drawShadow(item);
    device.endShadowPass();
    check(close(device.readShadowDepthForTests(1024, 1024), .5f, .0001f) &&
              close(device.readShadowDepthForTests(32, 32), 1),
          "shadow depth converts clip range and clears uncovered pixels before beginFrame");
    device.beginFrame(v);
    finish(device);
    auto masked = material({1, 0, 0, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Masked, true);
    value(*masked, "Opacity", {});
    item.materialProxy = masked;
    device.beginShadowPass(v, 0);
    device.drawShadow(item);
    device.endShadowPass();
    check(close(device.readShadowDepthForTests(1024, 1024), 1), "masked opacity also discards shadow depth");
    device.beginFrame(v);
    finish(device);
    item.mesh = std::make_shared<MeshResource>();
    item.mesh->load(root + "weighted.mesh");
    item.materialProxy = material({1, 0, 0, 1}, MaterialShadingModel::Unlit, MaterialBlendMode::Opaque, true);
    item.skinMatrices = {translationMatrix({-.6f, 0, 0}), translationMatrix({1, 0, 0})};
    device.beginShadowPass(v, 1);
    device.drawShadow(item);
    device.endShadowPass();
    check(close(device.readShadowDepthForTests(320, 1024, 1), 1) &&
              close(device.readShadowDepthForTests(1700, 1024, 1), .5f, .0001f),
          "weighted shadow geometry moves in an independently selected cascade");
    device.beginFrame(v);
    finish(device);
}
void postProcess(SdlVulkanRenderDevice& device)
{
    for (auto pass : {PostProcessPass::HierarchicalDepth, PostProcessPass::Gtao, PostProcessPass::Bloom,
                      PostProcessPass::TemporalResolve, PostProcessPass::ToneMap})
        device.executePostProcess(pass);
}
std::array<int, 4> ldrPixel(const std::vector<std::uint8_t>& pixels, int x, int top)
{
    const auto offset = (std::size_t(size - 1 - top) * size + x) * 4;
    return {pixels.at(offset), pixels.at(offset + 1), pixels.at(offset + 2), pixels.at(offset + 3)};
}
std::size_t ink(const std::vector<std::uint8_t>& pixels, int left, int top, int right, int bottom)
{
    std::size_t result = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
        {
            const auto p = ldrPixel(pixels, x, y);
            result += std::max({p[0], p[1], p[2]}) > 100;
        }
    return result;
}
void testPostProcessAndUi(SdlVulkanRenderDevice& device, RenderItem item)
{
    device.beginFrame(view());
    device.draw(item);
    postProcess(device);
    const auto plain = device.readSceneColorPixels();
    check(plain.size() == size * size * 4 && ldrPixel(plain, 32, 32)[0] > ldrPixel(plain, 32, 32)[1],
          "tone mapping produces actual encoded LDR scene pixels");
    check(device.getHzbLevelCount() == 6, "hierarchical depth creates a complete 64-to-1 pyramid");
    finish(device);
    device.setRetroPostProcess(true, 4, 4);
    device.beginFrame(view());
    device.draw(item);
    postProcess(device);
    device.executePostProcess(PostProcessPass::Pixelate);
    const auto retro = device.readSceneColorPixels();
    const auto normal = ldrPixel(plain, 32, 31), quantized = ldrPixel(retro, 32, 31);
    bool rounded = true;
    for (int c = 0; c < 3; ++c)
        rounded = rounded && std::abs(quantized[c] - int(std::floor(normal[c] / 255.f * 4 + .5f) / 4 * 255 + .5f)) <= 2;
    check(rounded && retro != plain, "retro postprocess quantizes LDR colors on the GPU");
    UIDrawCommand panel;
    panel.rect = {4, 4, 16, 16};
    panel.clipRect = {8, 8, 8, 8};
    panel.color = {1, 0, 0, 1};
    device.drawUi({panel});
    const auto overlay = device.readSceneColorPixels();
    check(ldrPixel(overlay, 10, 10)[0] > 250 && ldrPixel(overlay, 10, 10)[1] < 2 && ldrPixel(overlay, 5, 5)[0] < 2 &&
              ldrPixel(overlay, 10, 54)[0] < 2,
          "UI draws over retro output with top-left coordinates and exact clipping");
    finish(device);
    device.setRetroPostProcess(false, 4, 4);
    FontManager fonts;
    const auto latin = fonts.loadFace("tests/fixtures/fonts/noto_sans.ttf");
    const auto chinese = fonts.loadFace("tests/fixtures/fonts/noto_sans_sc_subset.otf");
    check(latin && chinese, "load real Latin and CJK font fixtures for Vulkan UI");
    if (latin && chinese)
    {
        fonts.registerFamily("vulkan-latin", {latin});
        fonts.registerFamily("vulkan-cjk", {chinese});
        fonts.setDefaultFamily("vulkan-latin");
        fonts.setFallbackFamilies({"vulkan-cjk"});
        UISystem ui;
        ui.setFontManager(&fonts);
        ui.setViewportSize(size, size);
        ui.AddLabel("latin", "AV fi", {2, 0, 60, 28}).fontSize = 18;
        ui.AddLabel("cjk", TextLayout::encodeUtf8({0x4e2d, 0x6587}), {2, 30, 60, 28}).fontSize = 18;
        const auto commands = ui.BuildDrawList();
        device.beginFrame(view());
        postProcess(device);
        device.drawUi(commands);
        const auto text = device.readSceneColorPixels();
        check(ink(text, 2, 0, 62, 28) > 35 && ink(text, 2, 30, 62, 58) > 50,
              "Vulkan UI rasterizes shaped Latin and CJK atlas glyphs as visible ink");
        const auto glyphCount = device.getGlyphAtlasStats().glyphCount;
        check(glyphCount >= 5, "Vulkan glyph atlas contains actual font glyphs");
        finish(device);
        device.beginFrame(view());
        postProcess(device);
        device.drawUi(commands);
        check(device.readSceneColorPixels() == text && device.getGlyphAtlasStats().glyphCount == glyphCount,
              "repeated UI frame reuses atlas glyphs without shifting coverage");
        finish(device);
    }
}
void testEnabledEffects(SdlVulkanRenderDevice& device, RenderItem item)
{
    const auto capture = [&](const FrameView& v, const std::vector<RenderItem>& items)
    {
        device.beginFrame(v);
        for (const auto& surface : items)
            device.draw(surface);
        postProcess(device);
        auto result = device.readSceneColorPixels();
        finish(device);
        return result;
    };
    item.materialProxy = material({5, 2, 1, 1});
    item.model = scaleMatrix({.3f, .3f, 1});
    const auto noBloom = capture(view(), {item});
    device.setBloomEnabled(true);
    const auto bloom = capture(view(), {item});
    check(ldrPixel(noBloom, 42, 32)[0] < 2 && ldrPixel(bloom, 42, 32)[0] > 10,
          "enabled Bloom spreads bright HDR energy into previously black neighboring pixels");
    device.setBloomEnabled(false);

    RenderItem floor = item, blocker = item;
    floor.model = scaleMatrix({3, 3, 1});
    floor.materialProxy = material({1, 1, 1, 1}, MaterialShadingModel::Lit);
    blocker.model = transformMatrix({0, 0, .12f}, {}, {.45f, .45f, 1});
    blocker.materialProxy = floor.materialProxy;
    const auto noAo = capture(view(), {floor, blocker});
    device.setGtaoEnabled(true);
    const auto ao = capture(view(), {floor, blocker});
    int maximumDarkening = 0, darkened = 0;
    for (std::size_t i = 0; i < ao.size(); i += 4)
    {
        const int delta = int(noAo[i]) - int(ao[i]);
        maximumDarkening = std::max(maximumDarkening, delta);
        darkened += delta >= 2;
    }
    std::cout << "GTAO contact darkening: max=" << maximumDarkening << ", pixels=" << darkened << '\n';
    check(maximumDarkening >= 2 && darkened >= 3,
          "enabled GTAO darkens indirect light beside a raised contact occluder");
    auto emission = material({0, 0, 0, 1}, MaterialShadingModel::Lit);
    value(*emission, "Emissive", {.3f, .2f, .1f, 0});
    floor.materialProxy = blocker.materialProxy = emission;
    const auto emissionAo = capture(view(), {floor, blocker});
    device.setGtaoEnabled(false);
    const auto emissionNoAo = capture(view(), {floor, blocker});
    check(emissionAo == emissionNoAo, "GTAO preserves self-emission while occluding indirect illumination");

    RenderItem red = item, left = item, right = item;
    red.materialProxy = material({1, .03f, .03f, 1});
    left.materialProxy = right.materialProxy = material({.05f, .7f, .05f, 1});
    red.model = transformMatrix({}, {-55, 0, 0}, {.04f, 3, 1});
    left.model = transformMatrix({-2.032f, 0, 0}, {-55, 0, 0}, {2.5f, 3, 1});
    right.model = transformMatrix({2.032f, 0, 0}, {-55, 0, 0}, {2.5f, 3, 1});
    for (auto* stripe : {&red, &left, &right})
        stripe->previousModel = stripe->model;
    const std::array<Vec2, 8> jitter{{{0, -1.f / 6},
                                      {-.25f, 1.f / 6},
                                      {.25f, -7.f / 18},
                                      {-.375f, -1.f / 18},
                                      {.125f, 5.f / 18},
                                      {-.125f, -5.f / 18},
                                      {.375f, 1.f / 18},
                                      {-.4375f, 7.f / 18}}};
    std::vector<std::vector<std::uint8_t>> references;
    device.setTaaEnabled(true);
    for (int run = 0; run < 2; ++run)
    {
        auto previous = view().projection * view().view;
        int changed = 0;
        for (std::size_t frame = 0; frame < jitter.size(); ++frame)
        {
            auto v = view();
            v.temporalJitter = {jitter[frame].x / size, jitter[frame].y / size};
            v.projection.at(0, 2) += v.temporalJitter.x * 2;
            v.projection.at(1, 2) += v.temporalJitter.y * 2;
            v.previousViewProjection = previous;
            v.resetTemporalHistory = run == 0 || frame == 0;
            previous = v.projection * v.view;
            const auto pixels = capture(v, {red, left, right});
            if (run == 0)
                references.push_back(pixels);
            else
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    changed += std::abs(int(pixels[i]) - int(references[frame][i])) > 8;
        }
        if (run == 1)
        {
            std::cout << "TAA sloped-stripe history contributions: " << changed << '\n';
            check(changed > 3, "enabled TAA accumulates sloped-surface history across real projection jitter");
        }
    }
    // Explicit reset must produce the same pixels as a fresh history at the same projection.
    auto reset = view();
    reset.temporalJitter = {jitter[0].x / size, jitter[0].y / size};
    reset.projection.at(0, 2) += reset.temporalJitter.x * 2;
    reset.projection.at(1, 2) += reset.temporalJitter.y * 2;
    check(capture(reset, {red, left, right}) == references.front(),
          "TAA history reset restores the independent first-frame image");
    device.setTaaEnabled(false);
}
void testStandaloneShaderValidation(SdlVulkanRenderDevice& device)
{
    const auto vertex = root + "validate-stage.vert", fragment = root + "validate-stage.frag";
    PlatformFile::writeAllText(vertex, "#version 330 core\nout vec3 vUv;\n"
                                       "void main(){vUv=vec3(.2,.4,.6);gl_Position=vec4(0,0,0,1);}\n");
    PlatformFile::writeAllText(fragment, "#version 330 core\nin vec3 vUv;\nout vec4 finalPixel;\n"
                                         "void main(){finalPixel=vec4(vUv,1);}\n");
    check(device.validateShader(vertex), "standalone vertex validation accepts a valid non-vUv stage interface");
    check(device.validateShader(fragment), "standalone fragment validation accepts a valid non-vUv stage interface");
    PlatformFile::writeAllText(vertex, "#version 330 core\nvoid main(){gl_Position=vec4(broken syntax);}\n");
    PlatformFile::writeAllText(fragment, "#version 330 core\nout vec4 p;void main(){p=missing_function();}\n");
    check(!device.validateShader(vertex) && !device.validateShader(fragment),
          "standalone shader validation rejects malformed vertex and fragment sources");
}
void testReload(SdlVulkanRenderDevice& device, RenderItem item)
{
    struct RestoreCwd
    {
        std::filesystem::path path = std::filesystem::current_path();
        ~RestoreCwd()
        {
            std::error_code ignored;
            std::filesystem::current_path(path, ignored);
        }
    } restore;
    const auto templates = ShaderPipeline::loadMaterialTemplates();
    const auto directory = restore.path / root / "IsolatedShaderReload";
    for (const auto& [file, source] : templates.files)
        PlatformFile::writeAllText(directory / "assets/shaders/material" / file, source);
    std::filesystem::current_path(directory);
    const std::filesystem::path path = "assets/shaders/material/DeferredLighting.frag";
    auto candidate = templates.files.at("DeferredLighting.frag");
    const std::string output = "outColor=vec4(color,base.a);";
    const auto position = candidate.find(output);
    if (position == std::string::npos)
        throw std::runtime_error("Cannot locate portable deferred final output");
    candidate.replace(position, output.size(), "outColor=vec4(0.125,0.25,0.5,1.0);");
    PlatformFile::writeAllText(path, candidate);
    check(device.reloadShaders(), "valid isolated deferred shader candidate reloads");
    const auto valid = pixel(render(device, item, view(), true));
    check(close(valid.x, .125f) && close(valid.y, .25f) && close(valid.z, .5f),
          "successful deferred shader reload changes real GPU pixels");
    PlatformFile::writeAllText(path, "invalid deferred shader candidate\n");
    check(!device.reloadShaders() && !device.GetLastShaderError().empty(),
          "invalid shader candidate reports reload failure");
    const auto preserved = pixel(render(device, item, view(), true));
    check(close(preserved.x, valid.x) && close(preserved.y, valid.y) && close(preserved.z, valid.z),
          "failed shader transaction preserves last valid deferred rendering");
    const auto forward = pixel(render(device, item));
    check(close(forward.x, .6f) && close(forward.y, .2f), "failed candidate also preserves forward material programs");
    PlatformFile::writeAllText(path, templates.files.at("DeferredLighting.frag"));
    check(device.reloadShaders() && close(pixel(render(device, item, view(), true)).x, .6f),
          "repairing shader source restores standard deferred rendering");
}
void testCacheRetirement(SdlVulkanRenderDevice& device, RenderItem item)
{
    check(device.reloadShaders(), "cache retirement fixture resets its watched template directory");
    const auto standard = item.materialProxy;
    for (int i = 0; i < 3; ++i)
    {
        auto p = std::make_shared<MaterialRenderProxy>(*standard);
        auto c = std::make_shared<CompiledMaterial>(*p->compiled);
        c->surfaceSource += "\n// retired draft " + std::to_string(i);
        c->canonicalProgramContent += " retired draft " + std::to_string(i);
        p->compiled = c;
        item.materialProxy = p;
        render(device, item);
    }
    const auto before = device.getCachedProgramCountForTests();
    item.materialProxy = standard;
    for (int i = 0; i < 125; ++i)
    {
        device.beginFrame(view());
        device.draw(item);
        finish(device);
    }
    check(device.getCachedProgramCountForTests() < before,
          "expired material drafts retire GPU programs at a completed frame boundary");
    device.trimGpuResources();
    device.beginFrame(view());
    check(device.getCachedProgramCountForTests() == 0, "explicit GPU trim also retires unused shader programs");
    finish(device);
}
void testMeshAllocationFailure(SdlVulkanRenderDevice& device, RenderItem item)
{
    item.mesh = std::make_shared<MeshResource>();
    item.mesh->load(root + "quad.mesh");
    failLineBuffer = true;
    device.beginFrame(view());
    bool failed = false;
    try
    {
        device.draw(item);
    }
    catch (const std::exception&)
    {
        failed = true;
    }
    failLineBuffer = false;
    check(failed, "line-index allocation failure propagates without a null write");
    device.draw(item);
    check(close(pixel(device.readHdrAttachment()).x, .6f), "mesh upload recovers after failed line-buffer allocation");
    finish(device);
}
} // namespace
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
                                                         const VkAllocationCallbacks* allocator, VkBuffer* buffer)
{
    if (failLineBuffer && info->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT && info->size == 48)
    {
        failLineBuffer = false;
        *buffer = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const auto create = reinterpret_cast<PFN_vkCreateBuffer>(vkGetDeviceProcAddr(device, "vkCreateBuffer"));
    return create(device, info, allocator, buffer);
}
int main()
{
    std::cout.setf(std::ios::unitbuf);
    SDL_setenv("TINYENGINE_VULKAN_VALIDATION", "1", 1);
    try
    {
        const auto item = fixtures();
        const auto reference = openGlReference(item);
        SdlVulkanRenderDevice device(size, size, "Vulkan render validation");
        const bool initialized = device.initialize();
        check(initialized, "initialize actual Vulkan device");
        if (!initialized)
        {
            std::cerr << device.GetLastShaderError() << '\n';
            return 2;
        }
        configure(device);
        check(device.isIndirectDrawSupported() && device.isGpuDrivenCullingSupported(),
              "actual Vulkan device advertises indirect draw and GPU-driven culling");
        const auto center = pixel(render(device, item));
        check(close(center.x, .6f) && close(center.y, .2f) && close(center.z, .1f),
              "Vulkan forward material writes expected linear HDR color");
        testLighting(device, item, reference);
        testResourceFallbacks(device, item);
        testGeometry(device, item);
        testSurfaceState(device, item);
        testOcclusionHistory(device, item);
        testTextures(device, item, reference);
        testParticles(device, item);
        testShadows(device, item);
        testPostProcessAndUi(device, item);
        testEnabledEffects(device, item);
        testStandaloneShaderValidation(device);
        testReload(device, item);
        testCacheRetirement(device, item);
        testMeshAllocationFailure(device, item);
        for (int i = 0; i < 8; ++i)
        {
            device.beginFrame(view());
            device.drawInstanced({item, item});
            finish(device);
        }
        const auto generation = device.getSceneGeneration();
        check(device.resizeSceneRenderTarget(80, 48), "request Vulkan scene resize");
        device.beginFrame(view());
        device.beginGBufferPass();
        device.draw(item);
        device.endGBufferPass();
        device.executeDeferredLighting();
        const auto resized = device.readHdrAttachment();
        check(resized.size() == 80 * 48 && close(pixel(resized, 40, 24, 80).x, .6f) &&
                  device.readDepthPixels().size() == 80 * 48 && device.getSceneGeneration() > generation,
              "resize recreates deferred color and depth targets and preserves rendered material");
        finish(device);
        check(device.getValidationErrorCount() == 0, "Vulkan validation reports zero rendering errors");
        device.shutdown();
        check(device.getValidationErrorCount() == 0, "Vulkan validation also reports zero resource-cleanup errors");
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
