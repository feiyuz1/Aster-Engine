#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/renderer.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/resource/resource_manager.h"
#include "tinyengine/ui/ui_system.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
thread_local bool countAllocations = false;
thread_local std::size_t allocationCount = 0;
} // namespace
void* operator new(std::size_t bytes)
{
    if (countAllocations)
        ++allocationCount;
    if (void* memory = std::malloc(bytes ? bytes : 1))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

using namespace tinyengine;
namespace
{
int failures = 0;
void APIENTRY GlDebug(GLenum, GLenum type, GLuint, GLenum, GLsizei, const GLchar* message, const void*)
{
    if (type == 0x824C)
        std::cerr << "OpenGL error: " << message << '\n';
}
void check(bool condition, const char* name)
{
    std::cout << (condition ? "PASS: " : "FAIL: ") << name << '\n';
    failures += !condition;
}

// 记录真实 Renderer 提交的绘制项，仅替代窗口和 GPU。
struct CaptureDevice : RenderDevice
{
    InputState input;
    float aspect = 16.0f / 9.0f;
    std::vector<RenderItem> scene, shadow;
    bool initialize() override
    {
        return true;
    }
    void shutdown() override {}
    bool pumpEvents() override
    {
        return true;
    }
    const InputState& GetInputState() const override
    {
        return input;
    }
    float getAspectRatio() const override
    {
        return aspect;
    }
    Vec2 getViewportSize() const override
    {
        return {1280, 720};
    }
    void beginShadowPass(const FrameView&, std::size_t) override {}
    void drawShadow(const RenderItem& item) override
    {
        shadow.push_back(item);
    }
    void drawShadowInstanced(const std::vector<RenderItem>& items) override
    {
        shadow.insert(shadow.end(), items.begin(), items.end());
    }
    void endShadowPass() override {}
    void beginFrame(const FrameView&) override
    {
        scene.clear();
    }
    void drawSkybox() override {}
    void draw(const RenderItem& item) override
    {
        scene.push_back(item);
    }
    void drawInstanced(const std::vector<RenderItem>& items) override
    {
        scene.insert(scene.end(), items.begin(), items.end());
    }
    void drawParticles(const std::vector<Particle>&) override {}
    void drawGpuParticles(const std::vector<GpuParticleEmitter>&) override {}
    void executePostProcess(PostProcessPass) override {}
    void endFrame() override {}
    void drawUi(const std::vector<UIDrawCommand>&) override {}
    void present() override {}
    std::size_t getResidentGpuBytes() const override
    {
        return 0;
    }
    void setGpuMemoryBudget(std::size_t) override {}
    void trimGpuResources() override {}
};
} // namespace

int main(int argc, char** argv)
{
    std::vector<RenderLight> directionalLights(2);
    countAllocations = true;
    const auto grid =
        ClusteredLightBuilder::build(directionalLights, Mat4::identity(), perspective(60, 16.0f / 9.0f, 0.1f, 1000),
                                     1280, 720, 0.1f, 1000, 16, 9, 24, 1);
    countAllocations = false;
    check(allocationCount < 32, "clustered lighting avoids allocating once per cluster");
    check(grid.lightIndices.size() == 3456 &&
              std::all_of(grid.counts.begin(), grid.counts.end(), [](auto count) { return count == 1; }) &&
              std::all_of(grid.lightIndices.begin(), grid.lightIndices.end(), [](auto index) { return index == 0; }),
          "cluster light limits and input order are preserved");
    const auto root = std::filesystem::absolute("saved/RenderStability") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    const std::string mesh = (root / "quad.mesh").generic_string();
    const std::string material = (root / "red.mat").generic_string();
    PlatformFile::writeAllText(mesh, "v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\nf 1 2 3\nf 1 3 4\n");
    PlatformFile::writeAllText(material, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 1 "
                                         "0 0 1\nparameter \"Emissive\" Vec3 4 0 0\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    Renderer renderer;
    renderer.setResourceManager(&resources);
    auto capture = std::make_unique<CaptureDevice>();
    auto* device = capture.get();
    check(renderer.initialize(std::move(capture)), "capture backend initializes");
    renderer.setCameraOverride(true, {}, {});
    World world;
    const Entity object = world.createEntity("Object");
    world.AddComponent(object, MeshComponent{mesh, material});
    const auto visible = [&](Vec3 position, Vec3 scale = {1, 1, 1})
    {
        auto* transform = world.GetComponent<TransformComponent>(object);
        transform->position = position;
        transform->scale = scale;
        renderer.renderFrame(world);
        return std::any_of(device->scene.begin(), device->scene.end(),
                           [&](const RenderItem& item) { return item.entity == object; });
    };
    check(visible({8.4f, 0, -10}), "horizontal 40-degree object remains visible in a 16:9 viewport");
    check(visible({14, 0, -10}, {10, 1, 1}), "partially visible large object survives when its origin is outside");
    check(!visible({0, 8, -10}), "object above the vertical frustum is culled");
    check(!visible({0, 0, 10}), "object behind the camera is culled");
    check(!visible({0, 0, -1100}), "object past the far plane is culled");
    device->aspect = 0.5f;
    check(!visible({5, 0, -10}), "portrait viewport culls objects outside its narrower horizontal frustum");
    device->aspect = 16.0f / 9.0f;
    const Entity parent = world.createEntity("Parent");
    world.GetComponent<TransformComponent>(parent)->position = {-30, 0, 0};
    world.setParent(object, parent);
    check(visible({30, 0, -10}), "parent transform is applied before visibility testing");
    world.setParent(object, kInvalidEntity);
    device->shadow.clear();
    check(!visible({-15, 0, -10}), "offscreen shadow caster is absent from scene draws");
    check(std::any_of(device->shadow.begin(), device->shadow.end(),
                      [&](const RenderItem& item) { return item.entity == object; }),
          "offscreen object is retained for shadow rendering");
    renderer.setCameraOverride(true, {}, {});
    renderer.renderFrame(world);
    const auto firstShadow = renderer.GetFrameView().shadowViewProjections[0];
    renderer.setCameraOverride(true, {0.00001f, 0, 0}, {});
    renderer.renderFrame(world);
    const auto secondShadow = renderer.GetFrameView().shadowViewProjections[0];
    check(std::abs(firstShadow.at(0, 3) - secondShadow.at(0, 3)) < 1e-8f &&
              std::abs(firstShadow.at(1, 3) - secondShadow.at(1, 3)) < 1e-8f,
          "sub-texel camera movement keeps shadow XY projection stable");
    world.destroyEntity(object);
    auto a = world.createEntity("DynamicA"), b = world.createEntity("DynamicB");
    for (auto e : {a, b})
    {
        world.AddComponent(e, MeshComponent{mesh, material});
        world.GetComponent<TransformComponent>(e)->position = {0, 0, -5};
    }
    auto red = renderer.GetMaterialSystem().CreateDynamicInstance(material),
         blue = renderer.GetMaterialSystem().CreateDynamicInstance(material);
    red->setParameter("BaseColor", Vec4{1, 0, 0, 1});
    blue->setParameter("BaseColor", Vec4{0, 0, 1, 1});
    renderer.setDynamicMaterial(a, red);
    renderer.setDynamicMaterial(b, blue);
    renderer.renderFrame(world);
    check(renderer.getLastDrawCallCount() == 2, "same asset with different dynamic values does not batch");
    blue->setParameter("BaseColor", Vec4{1, 0, 0, 1});
    renderer.renderFrame(world);
    check(renderer.getLastDrawCallCount() == 1, "same effective dynamic values share one batch");
    std::weak_ptr<DynamicMaterialInstance> destroyed = blue;
    blue.reset();
    world.destroyEntity(b);
    renderer.renderFrame(world);
    check(destroyed.expired(), "destroyed entity releases its generation-keyed dynamic instance");
    renderer.shutdown();

    // 真实 OpenGL：两块相同网格触发 GPU 实例剔除，测试快速转向的第一帧。
    Renderer gpuRenderer;
    gpuRenderer.setResourceManager(&resources);
    auto gpu = std::make_unique<SdlGlRenderDevice>(256, 144, "Render Stability Regression");
    auto* glDevice = gpu.get();
    if (!gpuRenderer.initialize(std::move(gpu)))
    {
        std::cerr << "OpenGL context unavailable; GPU regression cannot run.\n";
        return 2;
    }
    SDL_HideWindow(static_cast<SDL_Window*>(glDevice->getNativeWindow()));
    using DebugCallback = void(APIENTRY*)(decltype(&GlDebug), const void*);
    if (const auto debug = reinterpret_cast<DebugCallback>(SDL_GL_GetProcAddress("glDebugMessageCallback")))
    {
        glEnable(0x92E0);
        glEnable(0x8242);
        debug(GlDebug, nullptr);
    }
    check(glGetError() == GL_NO_ERROR, "OpenGL initialization does not generate errors");
    check(glDevice->isGpuDrivenCullingSupported(), "GPU culling regression uses the compute shader path");
    glDevice->enableSceneRenderTarget(true, 256, 144);
    glDevice->setBloomEnabled(false);
    glDevice->setGtaoEnabled(false);
    glDevice->setTaaEnabled(false);
    glDevice->setFxaaEnabled(false);
    World gpuWorld;
    for (float x : {1.4f, 2.6f})
    {
        const auto entity = gpuWorld.createEntity("Instance");
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {x, 0, -5};
        gpuWorld.AddComponent(entity, MeshComponent{mesh, material});
    }
    gpuRenderer.setCameraOverride(true, {}, {0, 90, 0});
    gpuRenderer.renderFrame(gpuWorld);
    gpuRenderer.setCameraOverride(true, {2, 0, 0}, {});
    gpuRenderer.renderFrame(gpuWorld);
    std::vector<unsigned char> pixels(256 * 144 * 4);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int redPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        redPixels += pixels[i] > 180 && pixels[i + 1] < 80 && pixels[i + 2] < 80;
    check(redPixels > 100, "GPU instances appear on the first frame after a camera turn");
    check(glGetError() == GL_NO_ERROR, "rendering does not generate OpenGL errors");

    const std::string greenMaterial = (root / "green.mat").generic_string();
    PlatformFile::writeAllText(greenMaterial, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" "
                                              "Vec4 0 1 0 1\nparameter \"Emissive\" Vec3 0 4 0\n");
    gpuWorld.clear();
    const auto background = gpuWorld.createEntity("Background");
    gpuWorld.AddComponent(background, MeshComponent{mesh, greenMaterial});
    gpuWorld.GetComponent<TransformComponent>(background)->position = {0, 0, -6};
    gpuWorld.GetComponent<TransformComponent>(background)->scale = {8, 5, 1};
    const auto foreground = gpuWorld.createEntity("Foreground");
    gpuWorld.AddComponent(foreground, MeshComponent{mesh, material});
    gpuWorld.GetComponent<TransformComponent>(foreground)->scale = {0.5f, 3, 1};
    gpuRenderer.setCameraOverride(true, {}, {});
    gpuWorld.GetComponent<TransformComponent>(foreground)->position = {0, 0, -3};
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::size_t edgePixel = (72 * 256 + 138) * 4;
    const int referenceRed = pixels[edgePixel], referenceGreen = pixels[edgePixel + 1];
    glDevice->setTaaEnabled(true);
    // 关闭再开启会清空历史；新历史中的柱子盖住下一帧刚露出的绿色背景。
    gpuWorld.GetComponent<TransformComponent>(foreground)->position.x = 0.1f;
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
        gpuRenderer.renderFrame(gpuWorld);
    gpuWorld.GetComponent<TransformComponent>(foreground)->position.x = 0;
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    check(referenceGreen > 180 && referenceRed < 80 && std::abs(int(pixels[edgePixel]) - referenceRed) < 12 &&
              std::abs(int(pixels[edgePixel + 1]) - referenceGreen) < 12,
          "TAA rejects foreground history on a newly revealed background pixel");

    // 144 高的 HZB 会出现 9 -> 5 的奇数层级；遮挡上下两端但露出中间。
    glDevice->setTaaEnabled(false);
    gpuWorld.clear();
    for (float x : {-0.7f, 0.7f})
    {
        const auto entity = gpuWorld.createEntity("Partly exposed instance");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, material});
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {x, -1.1518f, -5};
        gpuWorld.GetComponent<TransformComponent>(entity)->scale = {0.5f, 1.276f, 1};
    }
    for (const auto& positionScale : {std::pair<float, float>{-1.282f, 0.970f}, {0.0f, 1.247f}})
    {
        const auto entity = gpuWorld.createEntity("Occluding band");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, greenMaterial});
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {0, positionScale.first, -3};
        gpuWorld.GetComponent<TransformComponent>(entity)->scale = {8, positionScale.second, 1};
    }
    const auto countRed = [&]()
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int count = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            count += pixels[i] > 180 && pixels[i + 1] < 80 && pixels[i + 2] < 80;
        return count;
    };
    gpuRenderer.setCameraOverride(true, {0.01f, 0, 0}, {});
    gpuRenderer.renderFrame(gpuWorld);
    gpuRenderer.setCameraOverride(true, {}, {});
    gpuRenderer.renderFrame(gpuWorld);
    const int exposedPixels = countRed();
    gpuRenderer.renderFrame(gpuWorld);
    check(exposedPixels > 100 && countRed() >= exposedPixels * 0.95f,
          "stationary HZB retains the exposed middle of instances at rounded mip sizes");
    check(glGetError() == GL_NO_ERROR, "TAA and HZB history passes do not generate OpenGL errors");
    gpuWorld.clear();
    const std::string matteMaterial = (root / "matte.mat").generic_string();
    PlatformFile::writeAllText(
        matteMaterial,
        "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 0.08 0.20 0.55 1\nparameter "
        "\"Metallic\" Float 0.08\nparameter \"Roughness\" Float 0.65\nparameter \"Emissive\" Vec3 0.08 0.08 0.08\n");
    const auto slope = gpuWorld.createEntity("Unoccluded tilted plane");
    gpuWorld.AddComponent(slope, MeshComponent{mesh, matteMaterial});
    auto* slopeTransform = gpuWorld.GetComponent<TransformComponent>(slope);
    slopeTransform->position = {0, 0, -5};
    slopeTransform->rotation = {-55, 0, 0};
    slopeTransform->scale = {8, 8, 1};
    gpuRenderer.setCameraOverride(true, {}, {});
    glDevice->setGtaoEnabled(false);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto unoccludedReference = pixels;
    glDevice->setGtaoEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int planeDarkening = 0;
    for (int y = 55; y < 89; ++y)
        for (int x = 105; x < 151; ++x)
        {
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                planeDarkening =
                    std::max(planeDarkening, int(unoccludedReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Tilted plane darkening: " << planeDarkening << '\n';
    check(planeDarkening <= 3, "GTAO does not treat an unoccluded sloped surface as an occluder");
    glDevice->setGtaoEnabled(false);
    slopeTransform->rotation = {};
    const auto contactOccluder = gpuWorld.createEntity("Nearby contact occluder");
    gpuWorld.AddComponent(contactOccluder, MeshComponent{mesh, matteMaterial});
    // 独立验证环境光遮蔽，避免自发光掩盖正常的接触暗化。
    const auto contactMaterial = (root / "contact.mat").generic_string();
    PlatformFile::writeAllText(contactMaterial, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" "
                                                "Vec4 1 1 1 1\nparameter \"Roughness\" Float 1\n");
    gpuWorld.GetComponent<MeshComponent>(slope)->materialPath = contactMaterial;
    gpuWorld.GetComponent<MeshComponent>(contactOccluder)->materialPath = contactMaterial;
    const auto contactLight = gpuWorld.createEntity("Contact indirect light only");
    LightComponent contactLighting;
    contactLighting.intensity = 0;
    gpuWorld.AddComponent(contactLight, contactLighting);
    gpuWorld.GetComponent<TransformComponent>(contactOccluder)->position = {0, 0, -4.8f};
    gpuWorld.GetComponent<TransformComponent>(contactOccluder)->scale = {0.5f, 0.5f, 1};
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto contactReference = pixels;
    glDevice->setGtaoEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int contactDarkening = 0;
    for (int y = 62; y < 82; ++y)
        for (int x = 114; x < 142; ++x)
        {
            if (x >= 120 && x < 136 && y >= 64 && y < 80)
                continue;
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                contactDarkening =
                    std::max(contactDarkening, int(contactReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Contact darkening: " << contactDarkening << '\n';
    check(contactDarkening >= 3, "GTAO still produces occlusion beside a nearby raised surface");

    // AO 只能遮蔽间接光；近邻遮挡不能改变表面自身发出的光。
    const auto emissionOnly = (root / "emission-only.mat").generic_string();
    PlatformFile::writeAllText(emissionOnly, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" "
                                             "Vec4 0 0 0 1\nparameter \"Emissive\" Vec3 0.3 0.2 0.1\n");
    gpuWorld.GetComponent<MeshComponent>(slope)->materialPath = emissionOnly;
    glDevice->setGtaoEnabled(false);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto emissionReference = pixels;
    glDevice->setGtaoEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int emissionDarkening = 0;
    for (int y = 62; y < 82; ++y)
        for (int x = 114; x < 142; ++x)
        {
            if (x >= 120 && x < 136 && y >= 64 && y < 80)
                continue;
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                emissionDarkening =
                    std::max(emissionDarkening, int(emissionReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Emission darkening: " << emissionDarkening << '\n';
    check(emissionDarkening <= 1, "GTAO preserves emissive surfaces beside an occluder");

    // 长直墙与平地的接缝沿 X 完全相同，接触遮蔽不应形成竖直亮暗条。
    gpuWorld.clear();
    const auto diffuseOnly = (root / "diffuse-only.mat").generic_string();
    PlatformFile::writeAllText(diffuseOnly, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 "
                                            "0.5 0.6 0.8 1\nparameter \"Roughness\" Float 1\n");
    const auto wall = gpuWorld.createEntity("Uniform wall");
    gpuWorld.AddComponent(wall, MeshComponent{mesh, diffuseOnly});
    gpuWorld.GetComponent<TransformComponent>(wall)->position = {0, 0, -3};
    gpuWorld.GetComponent<TransformComponent>(wall)->scale = {12, 6, 1};
    const auto floor = gpuWorld.createEntity("Uniform floor");
    gpuWorld.AddComponent(floor, MeshComponent{mesh, diffuseOnly});
    gpuWorld.GetComponent<TransformComponent>(floor)->position = {0, -0.5f, -3};
    gpuWorld.GetComponent<TransformComponent>(floor)->rotation = {-90, 0, 0};
    gpuWorld.GetComponent<TransformComponent>(floor)->scale = {12, 12, 1};
    const auto ambientLight = gpuWorld.createEntity("Ambient only");
    LightComponent unlit;
    unlit.intensity = 0;
    gpuWorld.AddComponent(ambientLight, unlit);
    glDevice->enableSceneRenderTarget(true, 800, 450);
    pixels.resize(800 * 450 * 4);
    gpuRenderer.setCameraOverride(true, {}, {});
    glDevice->setGtaoEnabled(false);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto seamReference = pixels;
    glDevice->setGtaoEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int seamVariation = 0, seamContact = 0;
    for (int y = 165; y < 205; ++y)
    {
        int minimum = 255, maximum = 0;
        for (int x = 250; x < 550; ++x)
        {
            const auto index = (y * 800 + x) * 4;
            const int darkening = int(seamReference[index + 2]) - int(pixels[index + 2]);
            minimum = std::min(minimum, darkening);
            maximum = std::max(maximum, darkening);
        }
        seamVariation = std::max(seamVariation, maximum - minimum);
        seamContact = std::max(seamContact, maximum);
    }
    std::cout << "Wall seam variation: " << seamVariation << " contact: " << seamContact << '\n';
    check(seamVariation <= 2, "GTAO keeps a uniform wall-floor seam free of vertical bands");
    check(seamContact >= 3, "GTAO retains contact shading on the uniform wall-floor seam");
    glDevice->enableSceneRenderTarget(true, 256, 144);
    pixels.resize(256 * 144 * 4);
    // 斜面有真实近邻遮挡时，相邻像素不能交替丢失半分辨率的 AO。
    for (bool horizontalSlope : {false, true})
    {
        gpuWorld.clear();
        const auto tilted = gpuWorld.createEntity("Sloped contact receiver");
        gpuWorld.AddComponent(tilted, MeshComponent{mesh, contactMaterial});
        gpuWorld.GetComponent<TransformComponent>(tilted)->position = {0, 0, -5};
        const Vec3 rotation = horizontalSlope ? Vec3{0, 55, 0} : Vec3{-55, 0, 0};
        gpuWorld.GetComponent<TransformComponent>(tilted)->rotation = rotation;
        gpuWorld.GetComponent<TransformComponent>(tilted)->scale = {8, 8, 1};
        const auto band = gpuWorld.createEntity("Sloped raised band");
        gpuWorld.AddComponent(band, MeshComponent{mesh, contactMaterial});
        gpuWorld.GetComponent<TransformComponent>(band)->position = {0, 0, -4.8f};
        gpuWorld.GetComponent<TransformComponent>(band)->rotation = rotation;
        gpuWorld.GetComponent<TransformComponent>(band)->scale = horizontalSlope ? Vec3{0.5f, 8, 1} : Vec3{8, 0.5f, 1};
        const auto light = gpuWorld.createEntity("Slope indirect light only");
        gpuWorld.AddComponent(light, contactLighting);
        glDevice->setGtaoEnabled(false);
        gpuRenderer.renderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto slopeReference = pixels;
        glDevice->setGtaoEnabled(true);
        gpuRenderer.renderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int stripeHoles = 0, slopeContact = 0;
        const auto darkeningAt = [&](int coordinate)
        {
            const int index = (horizontalSlope ? 72 * 256 + coordinate : coordinate * 256 + 128) * 4 + 2;
            return int(slopeReference[index]) - int(pixels[index]);
        };
        const int center = horizontalSlope ? 128 : 72;
        for (int coordinate = center - 14; coordinate < center + 14; ++coordinate)
        {
            const int value = darkeningAt(coordinate);
            slopeContact = std::max(slopeContact, value);
            stripeHoles += value + 1 < std::min(darkeningAt(coordinate - 1), darkeningAt(coordinate + 1));
            std::cout << " " << value;
        }
        std::cout << " Sloped AO " << horizontalSlope << " holes=" << stripeHoles << " contact=" << slopeContact
                  << '\n';
        check(stripeHoles == 0 && slopeContact >= 2,
              "GTAO keeps contact shading on sloped receivers without alternating bands");
    }
    glDevice->setGtaoEnabled(false);
    gpuWorld.clear();
    const auto tinyAmbient = (root / "tiny-ambient.mat").generic_string();
    PlatformFile::writeAllText(tinyAmbient, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 "
                                            "1 0 0 1\nparameter \"Roughness\" Float 1\n");
    const auto farBackground = gpuWorld.createEntity("Occluded background behind tiny detail");
    gpuWorld.AddComponent(farBackground, MeshComponent{mesh, contactMaterial});
    gpuWorld.GetComponent<TransformComponent>(farBackground)->position = {0, 0, -5};
    gpuWorld.GetComponent<TransformComponent>(farBackground)->scale = {8, 5, 1};
    const auto backgroundBlock = gpuWorld.createEntity("Background contact blocker");
    gpuWorld.AddComponent(backgroundBlock, MeshComponent{mesh, contactMaterial});
    gpuWorld.GetComponent<TransformComponent>(backgroundBlock)->position = {-0.19f, 0, -4.8f};
    gpuWorld.GetComponent<TransformComponent>(backgroundBlock)->scale = {0.2f, 0.5f, 1};
    const auto tinyForeground = gpuWorld.createEntity("Isolated narrow diffuse foreground detail");
    gpuWorld.AddComponent(tinyForeground, MeshComponent{mesh, tinyAmbient});
    gpuWorld.GetComponent<TransformComponent>(tinyForeground)->position = {0.012f, 0.012f, -3};
    gpuWorld.GetComponent<TransformComponent>(tinyForeground)->scale = {0.02f, 0.12f, 1};
    const auto tinyLight = gpuWorld.createEntity("Tiny detail indirect light only");
    gpuWorld.AddComponent(tinyLight, contactLighting);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto tinyPixel = (72 * 256 + 128) * 4;
    const int tinyReference = pixels[tinyPixel];
    glDevice->setGtaoEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Tiny diffuse AO: " << tinyReference << " -> " << int(pixels[tinyPixel]) << '\n';
    check(tinyReference > 60 && tinyReference == int(pixels[tinyPixel]),
          "GTAO does not transfer background occlusion onto an isolated narrow diffuse foreground detail");
    glDevice->setGtaoEnabled(false);
    gpuWorld.clear();
    const auto detailBackground = gpuWorld.createEntity("Close background");
    gpuWorld.AddComponent(detailBackground, MeshComponent{mesh, greenMaterial});
    gpuWorld.GetComponent<TransformComponent>(detailBackground)->position = {0, 0, -5.03f};
    gpuWorld.GetComponent<TransformComponent>(detailBackground)->scale = {8, 5, 1};
    const auto detail = gpuWorld.createEntity("One pixel detail");
    gpuWorld.AddComponent(detail, MeshComponent{mesh, material});
    auto* detailTransform = gpuWorld.GetComponent<TransformComponent>(detail);
    detailTransform->position = {0.02005f, 0.02005f, -5};
    detailTransform->scale = {0.035f, 0.035f, 1};
    glDevice->setTaaEnabled(false);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto detailPixel = (72 * 256 + 128) * 4;
    const int detailRed = pixels[detailPixel], detailGreen = pixels[detailPixel + 1];
    glDevice->setTaaEnabled(true);
    detailTransform->position.x += 0.022f;
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
        gpuRenderer.renderFrame(gpuWorld);
    detailTransform->position.x -= 0.022f;
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Detail reference: " << detailRed << ',' << detailGreen << " TAA: " << int(pixels[detailPixel]) << ','
              << int(pixels[detailPixel + 1]) << '\n';
    check(detailRed > 180 && detailGreen < 80 && std::abs(detailRed - int(pixels[detailPixel])) < 12 &&
              std::abs(detailGreen - int(pixels[detailPixel + 1])) < 12,
          "TAA history filtering preserves a one-pixel detail above a close background");
    glDevice->setTaaEnabled(false);
    glDevice->setFxaaEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Detail FXAA: " << int(pixels[detailPixel]) << ',' << int(pixels[detailPixel + 1]) << '\n';
    check(std::abs(detailRed - int(pixels[detailPixel])) < 12 &&
              std::abs(detailGreen - int(pixels[detailPixel + 1])) < 12,
          "FXAA preserves isolated one-pixel geometry instead of blending it into its background");
    glDevice->setFxaaEnabled(false);
    gpuWorld.clear();
    const auto jitterCamera = gpuWorld.createEntity("Jitter camera");
    gpuWorld.AddComponent(jitterCamera, CameraComponent{});
    for (const auto& part : {std::pair<float, float>{-2.03f, 4.0f}, {0.0f, 0.06f}, {2.03f, 4.0f}})
    {
        const auto entity = gpuWorld.createEntity("Sloped striped surface");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, part.first == 0 ? material : greenMaterial});
        auto* transform = gpuWorld.GetComponent<TransformComponent>(entity);
        transform->position = {part.first, 0, -5};
        transform->rotation = {-55, 0, 0};
        transform->scale = {part.second, 8, 1};
    }
    gpuRenderer.setCameraOverride(false, {}, {});
    std::vector<std::vector<unsigned char>> jitterReferences;
    for (int frameIndex = 0; frameIndex < 8; ++frameIndex)
    {
        glDevice->setTaaEnabled(false);
        glDevice->setTaaEnabled(true);
        gpuRenderer.renderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        jitterReferences.push_back(pixels);
    }
    int accumulatedSlopePixels = 0;
    for (int frameIndex = 0; frameIndex < 8; ++frameIndex)
    {
        gpuRenderer.renderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 55; y < 89; ++y)
            for (int x = 124; x < 132; ++x)
            {
                const auto index = (y * 256 + x) * 4;
                accumulatedSlopePixels += std::abs(int(pixels[index]) - int(jitterReferences[frameIndex][index])) > 12;
            }
    }
    std::cout << "Sloped history contributions: " << accumulatedSlopePixels << '\n';
    check(accumulatedSlopePixels > 30, "TAA accumulates valid sloped-surface history with real projection jitter");
    // Pixel-art output uses a fixed sampling grid. Subpixel projection jitter must not
    // turn a stationary edge into a different three-pixel block on every frame.
    glDevice->setRetroPostProcess(true, 3.0f, 32.0f);
    for (int frameIndex = 0; frameIndex < 48; ++frameIndex)
        gpuRenderer.renderFrame(gpuWorld);
    std::vector<unsigned char> previousRetro;
    int unstableRetroPixels = 0;
    for (int frameIndex = 0; frameIndex < 8; ++frameIndex)
    {
        gpuRenderer.renderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (!previousRetro.empty())
            for (std::size_t index = 0; index < pixels.size(); index += 4)
                unstableRetroPixels += std::abs(int(pixels[index]) - int(previousRetro[index])) > 8;
        previousRetro = pixels;
    }
    int offPaletteChannels = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 4)
        for (int channel = 0; channel < 3; ++channel)
        {
            // 当前复古风格在色阶量化之后按屏幕行添加扫描线。
            const int row = static_cast<int>(index / 4 / 256) % 3;
            const float scanline = row < 1.5f ? 1.0f : 0.86f;
            const float level = std::round(pixels[index + channel] * 32.0f / (255.0f * scanline));
            offPaletteChannels +=
                std::abs(pixels[index + channel] - level * 255.0f * scanline / 32.0f) > 1.0f;
        }
    check(offPaletteChannels == 0, "editor scene texture contains the final quantized pixel-art output");

    UISystem overlayUi;
    overlayUi.AddPanel("render.regression.hud", {10.0f, 10.0f, 20.0f, 20.0f}, {0.4f, 0.2f, 0.6f, 1.0f});
    gpuRenderer.setUiSystem(&overlayUi);
    gpuRenderer.renderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::size_t hudPixel = ((144 - 20) * 256 + 20) * 4;
    check(std::abs(int(pixels[hudPixel]) - 102) < 2 &&
              std::abs(int(pixels[hudPixel + 1]) - 51) < 2 &&
              std::abs(int(pixels[hudPixel + 2]) - 153) < 2,
          "runtime HUD is composited into the editor scene texture after pixelation");
    gpuRenderer.setUiSystem(nullptr);
    std::cout << "Stationary pixel-art source changes: " << unstableRetroPixels << '\n';
    check(unstableRetroPixels == 0, "final pixel-art output stays stable with TAA enabled and a stationary camera");
    glDevice->setRetroPostProcess(false, 3.0f, 32.0f);
    gpuRenderer.renderFrame(gpuWorld);
    const auto restoredJitter = gpuRenderer.GetFrameView().temporalJitter;
    check(restoredJitter.x != 0.0f || restoredJitter.y != 0.0f,
          "leaving pixel-art mode restores native TAA projection samples");
    glDevice->setTaaEnabled(false);
    gpuRenderer.setCameraOverride(true, {}, {});
    gpuWorld.clear();
    const auto diagonal = gpuWorld.createEntity("Diagonal edge");
    gpuWorld.AddComponent(diagonal, MeshComponent{mesh, material});
    auto* diagonalTransform = gpuWorld.GetComponent<TransformComponent>(diagonal);
    diagonalTransform->position = {0, 0, -5};
    diagonalTransform->rotation = {0, 0, 27};
    diagonalTransform->scale = {3, 3, 1};
    const auto countAntialiasedPixels = [&]()
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int count = 0;
        for (std::size_t index = 0; index < pixels.size(); index += 4)
            count += pixels[index] > 20 && pixels[index] < 230;
        return count;
    };
    gpuRenderer.renderFrame(gpuWorld);
    const int aliasedPixels = countAntialiasedPixels();
    glDevice->setFxaaEnabled(true);
    gpuRenderer.renderFrame(gpuWorld);
    const int antialiasedPixels = countAntialiasedPixels();
    std::cout << "Diagonal edge intermediate pixels: " << aliasedPixels << " -> " << antialiasedPixels << '\n';
    check(antialiasedPixels > aliasedPixels + 20, "FXAA still smooths ordinary diagonal edges");
    glDevice->setFxaaEnabled(false);
    if (argc > 1)
    {
        const auto project = std::filesystem::absolute(argv[1]);
        gpuWorld.clear();
        resources.shutdown();
        resources.initialize(nullptr, project.parent_path() / "assets");
        check(SceneSerializer::load(gpuWorld, project.parent_path() / "assets/PacMan.scene"),
              "load reported PacMan scene");
        glDevice->enableSceneRenderTarget(true, 800, 450);
        glDevice->setTaaEnabled(false);
        glDevice->setBloomEnabled(false);
        glDevice->setGtaoEnabled(false);
        glDevice->setFxaaEnabled(false);
        pixels.resize(800 * 450 * 4);
        const auto readImage = [&]()
        {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->getSceneColorTexture()));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        bool projectStable = true;
        const bool lowView = argc > 2 && std::string(argv[2]) == "low";
        const bool singleDraw = argc > 2 && std::string(argv[2]) == "single";
        const bool faceProbe = argc > 2 && std::string(argv[2]) == "faces";
        const bool aoProbe = argc > 2 && std::string(argv[2]) == "ao";
        for (int frameIndex = 0; frameIndex < (singleDraw || faceProbe ? 48 : 16); ++frameIndex)
        {
            Vec3 position = lowView ? Vec3{7 + frameIndex * 0.025f, 2.5f, 18} : Vec3{7 + frameIndex * 0.025f, 17, 10};
            Vec3 rotation{lowView ? -10.0f : -80.0f, 0, 0};
            if (singleDraw)
            {
                const float angle = degreesToRadians(frameIndex * 7.5f);
                position = {7 + std::sin(angle) * 12, 5, 7 + std::cos(angle) * 12};
                rotation = {-22.619865f, -frameIndex * 7.5f, 0};
            }
            if (faceProbe)
                position = {7, 4.5f + frameIndex * 0.5f, 6};
            if (aoProbe)
            {
                position = {8.12f + frameIndex * 0.001f, 0.8f, 7.8f};
                rotation = {-40, -8, 0};
            }
            glDevice->setTaaEnabled(false);
            glDevice->setGtaoEnabled(false);
            glDevice->setFxaaEnabled(false);
            gpuRenderer.setCameraOverride(true, position, rotation);
            gpuRenderer.renderFrame(gpuWorld);
            const auto reference = readImage();
            if (faceProbe)
            {
                const auto& view = gpuRenderer.GetFrameView();
                const Mat4 vp = view.projection * view.view;
                float clip[4]{};
                for (int axis = 0; axis < 4; ++axis)
                    clip[axis] = vp.at(axis, 0) * 7 + vp.at(axis, 1) + vp.at(axis, 2) * 5 + vp.at(axis, 3);
                const int x = int((clip[0] / clip[3] * 0.5f + 0.5f) * 800),
                          y = int((clip[1] / clip[3] * 0.5f + 0.5f) * 450);
                if (x >= 0 && x < 800 && y >= 0 && y < 450)
                {
                    const int index = (y * 800 + x) * 4;
                    const float depth =
                        -(view.view.at(2, 0) * 7 + view.view.at(2, 1) + view.view.at(2, 2) * 5 + view.view.at(2, 3));
                    std::cout << "Face at depth " << depth << ": " << int(reference[index]) << ','
                              << int(reference[index + 1]) << ',' << int(reference[index + 2]) << '\n';
                }
            }
            if (argc > 3)
            {
                glDevice->setTaaEnabled(true);
                glDevice->setGtaoEnabled(std::string(argv[3]) == "all");
                glDevice->setFxaaEnabled(std::string(argv[3]) == "all");
                gpuRenderer.setCameraOverride(true, position + Vec3{-0.025f, 0, 0}, rotation);
                for (int warmup = 0; warmup < 3; ++warmup)
                    gpuRenderer.renderFrame(gpuWorld);
                gpuRenderer.setCameraOverride(true, position, rotation);
            }
            gpuRenderer.renderFrame(gpuWorld);
            auto stationary = readImage();
            if (singleDraw)
            {
                auto view = gpuRenderer.GetFrameView();
                view.resetTemporalHistory = true;
                glDevice->beginFrame(view);
                gpuWorld.GetComponentManager().ForEach<MeshComponent>(
                    [&](Entity entity, MeshComponent& component)
                    {
                        RenderItem item;
                        item.mesh = resources.load<MeshResource>(component.meshPath);
                        item.material = resources.load<MaterialResource>(component.materialPath);
                        item.model = gpuWorld.getWorldMatrix(entity);
                        item.previousModel = item.model;
                        glDevice->draw(item);
                    });
                for (auto pass : {PostProcessPass::HierarchicalDepth, PostProcessPass::Gtao, PostProcessPass::Bloom,
                                  PostProcessPass::TemporalResolve, PostProcessPass::ToneMap})
                    glDevice->executePostProcess(pass);
                glDevice->endFrame();
                stationary = readImage();
            }
            int changed = 0, maximum = 0;
            for (std::size_t index = 0; index < pixels.size(); index += 4)
            {
                int delta = 0;
                for (int channel = 0; channel < 3; ++channel)
                    delta =
                        std::max(delta, std::abs(int(reference[index + channel]) - int(stationary[index + channel])));
                changed += delta > 4;
                maximum = std::max(maximum, delta);
            }
            std::cout << "PacMan frame " << frameIndex << ": changed=" << changed << " max=" << maximum << '\n';
            projectStable = projectStable && changed <= (singleDraw ? 20 : 0);
            if (frameIndex == 8)
            {
                auto save = [&](const std::vector<unsigned char>& data, const char* name)
                {
                    auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char*>(data.data()), 800,
                                                                       450, 32, 800 * 4, SDL_PIXELFORMAT_RGBA32);
                    SDL_SaveBMP(surface, (root / name).string().c_str());
                    SDL_FreeSurface(surface);
                };
                save(reference, "pacman-reference.bmp");
                save(stationary, "pacman-stationary.bmp");
                std::cout << "Images: " << root.string() << '\n';
            }
        }
        if (argc <= 3)
            check(projectStable, "reported PacMan scene retains visible geometry when occlusion history becomes valid");
    }
    gpuRenderer.shutdown();
    resources.shutdown();
    return failures ? 1 : 0;
}
