#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/resource/resource_manager.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace tinyengine;
namespace
{
int failures = 0;
void check(bool result, const char* label)
{
    std::cout << (result ? "[PASS] " : "[FAIL] ") << label << '\n';
    failures += !result;
}
bool close(float a, float b, float tolerance = .006f)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < tolerance;
}
std::shared_ptr<MaterialRenderProxy> proxy(MaterialShadingModel shading = MaterialShadingModel::Lit,
                                           MaterialBlendMode blend = MaterialBlendMode::Opaque)
{
    auto definition = makeBuiltinPbrDefinition();
    definition.renderState.shadingModel = shading;
    definition.renderState.blendMode = blend;
    auto p = std::make_shared<MaterialRenderProxy>();
    p->compiled = MaterialCompiler{}.compile(definition).material;
    p->state = definition.renderState;
    for (const auto& slot : p->compiled->layout.numericSlots)
    {
        const auto value = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                        [&](const auto& v) { return v.name == slot.name; });
        p->numericSlots.push_back(materialNumericSlot(value->defaultValue));
    }
    return p;
}
void value(MaterialRenderProxy& p, const std::string& name, Vec4 v)
{
    for (std::size_t i = 0; i < p.compiled->layout.numericSlots.size(); ++i)
        if (p.compiled->layout.numericSlots[i].name == name)
            p.numericSlots[i] = v;
}
FrameView view()
{
    FrameView v;
    v.cameraPosition = {0, 0, 3};
    v.view = lookAt(v.cameraPosition, {}, {0, 1, 0});
    v.projection = perspective(60, 1, .1f, 100);
    v.previousViewProjection = v.projection * v.view;
    v.lightCount = 1;
    v.lights[0].type = RenderLightType::Point;
    v.lights[0].position = {0, 0, 2};
    v.lights[0].intensity = 4;
    v.clusteredLights.tilesX = v.clusteredLights.tilesY = v.clusteredLights.slicesZ = 1;
    v.clusteredLights.offsets = {0};
    v.clusteredLights.counts = {1};
    v.clusteredLights.lightIndices = {0};
    return v;
}
Vec4 readAttachment(int attachment, int x = 32, int y = 32)
{
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
    Vec4 pixel{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, &pixel);
    return pixel;
}
template <class Device> void runDeferred(Device& device, RenderItem item)
{
    if constexpr (requires(Device& d) {
                      d.supportsDeferredRendering();
                      d.beginGBufferPass();
                      d.endGBufferPass();
                      d.executeDeferredLighting();
                  })
    {
        check(device.supportsDeferredRendering(), "real OpenGL device provides G-buffer and lighting targets");
        if (!device.supportsDeferredRendering())
            return;
        auto p = proxy();
        value(*p, "BaseColor", {.4f, .2f, .1f, 1});
        value(*p, "Metallic", {.6f, 0, 0, 0});
        value(*p, "Roughness", {.35f, 0, 0, 0});
        value(*p, "Emissive", {.1f, .05f, .02f, 0});
        item.materialProxy = p;
        const auto v = view();
        device.beginFrame(v);
        device.draw(item);
        const auto reference = device.ReadHdrAttachmentForTests();
        device.beginFrame(v);
        device.beginGBufferPass();
        device.draw(item);
        const auto base = readAttachment(0), normal = readAttachment(1), emission = readAttachment(2);
        check(close(base.x, .4f) && close(base.y, .2f) && close(base.z, .1f), "G-buffer stores linear surface color");
        check(close(normal.z, 1) && close(normal.w, .35f), "G-buffer stores world normal and roughness");
        check(close(emission.x, .1f) && close(emission.w, .6f), "G-buffer stores emissive and metallic");
        device.endGBufferPass();
        const auto beforeLight = device.ReadHdrAttachmentForTests();
        check(close(beforeLight[32 * 64 + 32].x, 0), "geometry pass leaves HDR unlit");
        device.executeDeferredLighting();
        const auto lit = device.ReadHdrAttachmentForTests();
        float error = 0;
        for (std::size_t i = 0; i < lit.size(); ++i)
            error = std::max(error, std::max({std::abs(lit[i].x - reference[i].x), std::abs(lit[i].y - reference[i].y),
                                              std::abs(lit[i].z - reference[i].z)}));
        check(error < .012f && lit[32 * 64 + 32].x > .1f, "independent deferred lighting agrees with Forward+ HDR");
        for (bool instanced : {false, true})
        {
            item.materialProxy = proxy(MaterialShadingModel::Unlit);
            value(*const_cast<MaterialRenderProxy*>(item.materialProxy.get()), "BaseColor", {.7f, .1f, .2f, 1});
            device.beginFrame(v);
            device.beginGBufferPass();
            if (instanced)
                device.drawInstanced({item, item});
            else
            {
                item.skinMatrices = {Mat4::identity()};
                device.draw(item);
                item.skinMatrices.clear();
            }
            device.endGBufferPass();
            device.executeDeferredLighting();
            const auto pixels = device.ReadHdrAttachmentForTests();
            check(close(pixels[32 * 64 + 32].x, .7f), "Unlit skinned and instanced G-buffer variants preserve color");
        }
        {
            auto weighted = std::make_shared<MeshResource>();
            check(weighted->load("saved/DeferredRender/weighted.mesh"), "load actual weighted skin fixture");
            const auto original = item.mesh;
            item.mesh = weighted;
            item.skinMatrices = {Mat4::identity()};
            item.skinMatrices[0].at(0, 3) = .6f;
            device.beginFrame(v);
            device.beginGBufferPass();
            device.draw(item);
            float left = 0, right = 1;
            glReadPixels(20, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &left);
            glReadPixels(50, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &right);
            check(close(left, 1) && right < 1, "bone weights and moving skin transform change G-buffer coverage");
            device.endGBufferPass();
            device.executeDeferredLighting();
            check(close(device.ReadHdrAttachmentForTests()[32 * 64 + 50].x, .7f),
                  "deferred light resolves moved skinned surface");
            item.mesh = original;
            item.skinMatrices.clear();
        }
        p = proxy(MaterialShadingModel::Lit, MaterialBlendMode::Masked);
        value(*p, "Opacity", {0, 0, 0, 0});
        item.materialProxy = p;
        device.beginFrame(v);
        device.beginGBufferPass();
        device.draw(item);
        float depth = 0;
        glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        check(close(depth, 1), "masked holes do not write G-buffer depth");
        device.endGBufferPass();
        device.executeDeferredLighting();
        // The previous frame's bright material must not survive a masked/empty frame.
        check(close(device.ReadHdrAttachmentForTests()[32 * 64 + 32].x, 0),
              "empty pixels do not reuse stale G-buffer data");
        check(device.resizeSceneRenderTarget(80, 48), "resize recreates deferred targets");
        device.beginFrame(v);
        device.beginGBufferPass();
        device.endGBufferPass();
        device.executeDeferredLighting();
        check(device.ReadHdrAttachmentForTests().size() == 80 * 48, "deferred output follows viewport dimensions");
        {
            const auto cwd = std::filesystem::current_path();
            const auto folder = cwd / "saved/DeferredRender/HotReload";
            const auto templates = ShaderPipeline::loadMaterialTemplates();
            for (const auto& file : templates.files)
                PlatformFile::writeAllText(folder / "assets/shaders/material" / file.first, file.second);
            std::filesystem::remove(folder / "assets/shaders/default.vert");
            std::filesystem::remove(folder / "assets/shaders/default.frag");
            std::filesystem::current_path(folder);
            auto source = templates.files.at("DeferredLighting.frag");
            const std::string output = "outColor=vec4(color,base.a);";
            const auto position = source.find(output);
            source.replace(position, output.size(), "outColor=vec4(vec3(0.125,0.25,0.5),1.0);");
            const auto path = std::filesystem::path("assets/shaders/material/DeferredLighting.frag");
            PlatformFile::writeAllText(path, source);
            device.pumpEvents();
            item.materialProxy = proxy(MaterialShadingModel::Unlit);
            const auto render = [&]
            {
                device.beginFrame(v);
                device.beginGBufferPass();
                device.draw(item);
                device.endGBufferPass();
                device.executeDeferredLighting();
                return device.ReadHdrAttachmentForTests()[24 * 80 + 40];
            };
            check(close(render().x, .125f), "deferred-only shader edits hot reload through pumpEvents");
            PlatformFile::writeAllText(path, "invalid deferred shader candidate");
            device.pumpEvents();
            check(close(render().x, .125f), "failed deferred hot reload keeps the last valid program");
            auto legacy = item;
            legacy.materialProxy.reset();
            const auto renderLegacy = [&]
            {
                device.beginFrame(v);
                device.draw(legacy);
                return device.ReadHdrAttachmentForTests()[24 * 80 + 40];
            };
            const auto previousLegacy = renderLegacy();
            PlatformFile::writeAllText("assets/shaders/default.vert",
                                       "#version 330 core\nlayout(location=0) in vec3 aPosition; uniform mat4 uMvp;"
                                       "void main(){gl_Position=uMvp*vec4(aPosition,1);}\n");
            PlatformFile::writeAllText(
                "assets/shaders/default.frag",
                "#version 330 core\nlayout(location=0) out vec4 outColor;void main(){outColor=vec4(.95,.05,.05,1);}\n");
            check(!device.reloadShaders(), "invalid deferred candidate rejects whole shader reload");
            const auto unchangedLegacy = renderLegacy();
            check(close(unchangedLegacy.x, previousLegacy.x) && close(unchangedLegacy.z, previousLegacy.z),
                  "failed shader transaction also preserves legacy/default program");
            PlatformFile::writeAllText(path, templates.files.at("DeferredLighting.frag"));
            device.pumpEvents();
            check(close(render().z, 1), "repairing deferred source resumes hot reload");
            std::filesystem::current_path(cwd);
        }
        check(glGetError() == GL_NO_ERROR, "deferred passes and resize generate no OpenGL errors");
    }
    else
        check(false, "OpenGL device exposes actual deferred pass execution");
}
template <class R> void runRenderer()
{
    if constexpr (requires(R& r) {
                      r.getRenderPath();
                      r.getActiveRenderPath();
                  })
    {
        using Path = decltype(std::declval<R>().getRenderPath());
        const auto deferred = static_cast<Path>(1), forward = static_cast<Path>(0);
        ResourceManager resources;
        resources.initialize(nullptr, "saved/DeferredRender");
        PlatformFile::writeAllText(
            "saved/DeferredRender/lit.mat",
            "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 .4 .2 .1 1\n");
        R renderer;
        renderer.setResourceManager(&resources);
        auto gpu = std::make_unique<SdlGlRenderDevice>(64, 64, "Deferred pipeline regression");
        auto* device = gpu.get();
        check(renderer.initialize(std::move(gpu)), "initialize full deferred Renderer");
        SDL_HideWindow(static_cast<SDL_Window*>(device->getNativeWindow()));
        device->enableSceneRenderTarget(true, 64, 64);
        device->setTaaEnabled(false);
        device->setBloomEnabled(false);
        device->setGtaoEnabled(false);
        device->setFxaaEnabled(false);
        renderer.setCameraOverride(true, {}, {});
        World world;
        auto object = world.createEntity("Deferred opaque");
        world.AddComponent(object, MeshComponent{"quad.mesh", "lit.mat"});
        world.GetComponent<TransformComponent>(object)->position = {0, 0, -3};
        renderer.renderFrame(world);
        check(renderer.getRenderPath() == deferred && renderer.getActiveRenderPath() == deferred,
              "Renderer defaults to actual deferred rendering");
        const auto delayed = device->ReadHdrAttachmentForTests();
        const auto deferredPasses = renderer.GetLastRenderGraph().getPassCount();
        renderer.setRenderPath(forward);
        renderer.renderFrame(world);
        check(renderer.getActiveRenderPath() == forward && renderer.GetFrameView().resetTemporalHistory,
              "switching to Forward+ invalidates temporal and occlusion histories");
        const auto reference = device->ReadHdrAttachmentForTests();
        check(deferredPasses > renderer.GetLastRenderGraph().getPassCount() && delayed[32 * 64 + 32].x > .001f &&
                  close(delayed[32 * 64 + 32].x, reference[32 * 64 + 32].x, .02f),
              "full RenderGraph runs deferred geometry and independent lighting");
        const auto alphaMaterial = [&](const std::string& path, Vec4 color)
        {
            auto definition = makeEditablePbrDefinition();
            definition.graph.GetRenderState().shadingModel = MaterialShadingModel::Unlit;
            definition.graph.GetRenderState().blendMode = MaterialBlendMode::Alpha;
            for (auto& parameter : definition.graph.GetParameters())
                if (parameter.name == "BaseColor")
                    parameter.defaultValue = color;
                else if (parameter.name == "Opacity")
                    parameter.defaultValue = .5f;
            PlatformFile::writeAllText("saved/DeferredRender/" + path, definition.graph.serialize());
        };
        alphaMaterial("green.mat", {0, 1, 0, 1});
        alphaMaterial("blue.mat", {0, 0, 1, 1});
        for (int layer = 0; layer < 2; ++layer)
        {
            const auto e = world.createEntity("Alpha layer");
            world.AddComponent(e, MeshComponent{"quad.mesh", layer ? "blue.mat" : "green.mat"});
            world.GetComponent<TransformComponent>(e)->position = {0, 0, layer ? -2.8f : -2.9f};
        }
        Vec4 mixed[2];
        for (int path = 0; path < 2; ++path)
        {
            renderer.setRenderPath(path ? deferred : forward);
            renderer.renderFrame(world);
            mixed[path] = device->ReadHdrAttachmentForTests()[32 * 64 + 32];
        }
        check(close(mixed[0].z, mixed[1].z, .02f) && close(mixed[1].z, .5f + .25f * reference[32 * 64 + 32].z, .02f) &&
                  close(mixed[1].y, .25f + .25f * reference[32 * 64 + 32].y, .02f),
              "mixed Alpha layers render exactly once in far-to-near order over deferred opaque geometry");
        auto dynamic = renderer.GetMaterialSystem().CreateDynamicInstance("lit.mat");
        dynamic->setParameter("Emissive", Vec3{1, 0, 0});
        renderer.setDynamicMaterial(object, dynamic);
        renderer.renderFrame(world);
        check(device->ReadHdrAttachmentForTests()[32 * 64 + 32].x > mixed[1].x + .2f,
              "dynamic surface parameters update deferred geometry under transparent layers");
        for (int i = 0; i < 4; ++i)
        {
            renderer.setRenderPath(i % 2 ? forward : deferred);
            renderer.renderFrame(world);
        }
        renderer.setRenderPath(deferred);
        renderer.setViewportRenderMode(ViewportRenderMode::Wireframe);
        renderer.renderFrame(world);
        check(renderer.getActiveRenderPath() == forward && renderer.getRenderPath() == deferred,
              "Wireframe uses Forward+ without losing deferred preference");
        renderer.setViewportRenderMode(ViewportRenderMode::Lit);
        renderer.renderFrame(world);
        check(renderer.getActiveRenderPath() == deferred, "Lit restores deferred rendering");
        check(glGetError() == GL_NO_ERROR, "full pipeline and repeated switches are GL error free");
        renderer.shutdown();
        R nullRenderer;
        nullRenderer.initialize(std::make_unique<NullRenderDevice>());
        World empty;
        nullRenderer.renderFrame(empty);
        check(nullRenderer.getActiveRenderPath() == forward, "Null backend safely falls back to Forward+");
        nullRenderer.shutdown();
        resources.shutdown();
    }
    else
        check(false, "Renderer schedules and switches actual deferred rendering");
}
} // namespace
int main()
{
    std::cout.setf(std::ios::unitbuf);
    PlatformFile::writeAllText("saved/DeferredRender/quad.mesh",
                               "v -0.8 -0.8 0\nv 0.8 -0.8 0\nv 0.8 0.8 0\nv -0.8 0.8 0\n"
                               "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
    SdlGlRenderDevice device(64, 64, "Deferred rendering regression");
    if (!device.initialize())
        return 2;
    SDL_HideWindow(static_cast<SDL_Window*>(device.getNativeWindow()));
    check(device.enableSceneRenderTarget(true, 64, 64), "create scene targets");
    RenderItem item;
    item.mesh = std::make_shared<MeshResource>();
    check(item.mesh->load("saved/DeferredRender/quad.mesh"), "load deferred GPU fixture");
    auto weighted = PlatformFile::readAllText("saved/DeferredRender/quad.mesh");
    weighted.insert(weighted.find("f 1/"),
                    "vw 1 0 1 0 0 0 0 0 0\nvw 2 0 1 0 0 0 0 0 0\nvw 3 0 1 0 0 0 0 0 0\nvw 4 0 1 0 0 0 0 0 0\n");
    PlatformFile::writeAllText("saved/DeferredRender/weighted.mesh", weighted);
    runDeferred(device, item);
    device.shutdown();
    runRenderer<Renderer>();
    return failures ? 1 : 0;
}
