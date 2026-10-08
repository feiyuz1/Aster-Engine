#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/Renderer.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
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
void *operator new(std::size_t bytes)
{
    if (countAllocations)
        ++allocationCount;
    if (void *memory = std::malloc(bytes ? bytes : 1))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept
{
    std::free(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

using namespace MiniEngine;
namespace
{
int failures = 0;
void APIENTRY GlDebug(GLenum, GLenum type, GLuint, GLenum, GLsizei, const GLchar *message, const void *)
{
    if (type == 0x824C)
        std::cerr << "OpenGL error: " << message << '\n';
}
void Check(bool condition, const char *name)
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
    bool Initialize() override
    {
        return true;
    }
    void Shutdown() override
    {
    }
    bool PumpEvents() override
    {
        return true;
    }
    const InputState &GetInputState() const override
    {
        return input;
    }
    float GetAspectRatio() const override
    {
        return aspect;
    }
    Vec2 GetViewportSize() const override
    {
        return {1280, 720};
    }
    void BeginShadowPass(const FrameView &, std::size_t) override
    {
    }
    void DrawShadow(const RenderItem &item) override
    {
        shadow.push_back(item);
    }
    void DrawShadowInstanced(const std::vector<RenderItem> &items) override
    {
        shadow.insert(shadow.end(), items.begin(), items.end());
    }
    void EndShadowPass() override
    {
    }
    void BeginFrame(const FrameView &) override
    {
        scene.clear();
    }
    void DrawSkybox() override
    {
    }
    void Draw(const RenderItem &item) override
    {
        scene.push_back(item);
    }
    void DrawInstanced(const std::vector<RenderItem> &items) override
    {
        scene.insert(scene.end(), items.begin(), items.end());
    }
    void DrawParticles(const std::vector<Particle> &) override
    {
    }
    void DrawGpuParticles(const std::vector<GpuParticleEmitter> &) override
    {
    }
    void ExecutePostProcess(PostProcessPass) override
    {
    }
    void EndFrame() override
    {
    }
    void DrawUi(const std::vector<UIDrawCommand> &) override
    {
    }
    void Present() override
    {
    }
    std::size_t GetResidentGpuBytes() const override
    {
        return 0;
    }
    void SetGpuMemoryBudget(std::size_t) override
    {
    }
    void TrimGpuResources() override
    {
    }
};
} // namespace

int main(int argc, char **argv)
{
    std::vector<RenderLight> directionalLights(2);
    countAllocations = true;
    const auto grid = ClusteredLightBuilder::Build(directionalLights, Mat4::Identity(), Perspective(60, 16.0f / 9.0f, 0.1f, 1000), 1280,
                                                   720, 0.1f, 1000, 16, 9, 24, 1);
    countAllocations = false;
    Check(allocationCount < 32, "clustered lighting avoids allocating once per cluster");
    Check(grid.lightIndices.size() == 3456 && std::all_of(grid.counts.begin(), grid.counts.end(), [](auto count) { return count == 1; }) &&
              std::all_of(grid.lightIndices.begin(), grid.lightIndices.end(), [](auto index) { return index == 0; }),
          "cluster light limits and input order are preserved");
    const auto root =
        std::filesystem::absolute("Saved/RenderStability") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    const std::string mesh = (root / "quad.mesh").generic_string();
    const std::string material = (root / "red.mat").generic_string();
    PlatformFile::WriteAllText(mesh, "v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\nf 1 2 3\nf 1 3 4\n");
    PlatformFile::WriteAllText(material, "color 1 0 0 1\nemissive 4 0 0\n");
    ResourceManager resources;
    resources.Initialize(nullptr, root);
    Renderer renderer;
    renderer.SetResourceManager(&resources);
    auto capture = std::make_unique<CaptureDevice>();
    auto *device = capture.get();
    Check(renderer.Initialize(std::move(capture)), "capture backend initializes");
    renderer.SetCameraOverride(true, {}, {});
    World world;
    const Entity object = world.CreateEntity("Object");
    world.AddComponent(object, MeshComponent{mesh, material});
    const auto visible = [&](Vec3 position, Vec3 scale = {1, 1, 1}) {
        auto *transform = world.GetComponent<TransformComponent>(object);
        transform->position = position;
        transform->scale = scale;
        renderer.RenderFrame(world);
        return std::any_of(device->scene.begin(), device->scene.end(), [&](const RenderItem &item) { return item.entity == object; });
    };
    Check(visible({8.4f, 0, -10}), "horizontal 40-degree object remains visible in a 16:9 viewport");
    Check(visible({14, 0, -10}, {10, 1, 1}), "partially visible large object survives when its origin is outside");
    Check(!visible({0, 8, -10}), "object above the vertical frustum is culled");
    Check(!visible({0, 0, 10}), "object behind the camera is culled");
    Check(!visible({0, 0, -1100}), "object past the far plane is culled");
    device->aspect = 0.5f;
    Check(!visible({5, 0, -10}), "portrait viewport culls objects outside its narrower horizontal frustum");
    device->aspect = 16.0f / 9.0f;
    const Entity parent = world.CreateEntity("Parent");
    world.GetComponent<TransformComponent>(parent)->position = {-30, 0, 0};
    world.SetParent(object, parent);
    Check(visible({30, 0, -10}), "parent transform is applied before visibility testing");
    world.SetParent(object, InvalidEntity);
    device->shadow.clear();
    Check(!visible({-15, 0, -10}), "offscreen shadow caster is absent from scene draws");
    Check(std::any_of(device->shadow.begin(), device->shadow.end(), [&](const RenderItem &item) { return item.entity == object; }),
          "offscreen object is retained for shadow rendering");
    renderer.SetCameraOverride(true, {}, {});
    renderer.RenderFrame(world);
    const auto firstShadow = renderer.GetFrameView().shadowViewProjections[0];
    renderer.SetCameraOverride(true, {0.00001f, 0, 0}, {});
    renderer.RenderFrame(world);
    const auto secondShadow = renderer.GetFrameView().shadowViewProjections[0];
    Check(std::abs(firstShadow.At(0, 3) - secondShadow.At(0, 3)) < 1e-8f && std::abs(firstShadow.At(1, 3) - secondShadow.At(1, 3)) < 1e-8f,
          "sub-texel camera movement keeps shadow XY projection stable");
    renderer.Shutdown();

    // 真实 OpenGL：两块相同网格触发 GPU 实例剔除，测试快速转向的第一帧。
    Renderer gpuRenderer;
    gpuRenderer.SetResourceManager(&resources);
    auto gpu = std::make_unique<SdlGlRenderDevice>(256, 144, "Render Stability Regression");
    auto *glDevice = gpu.get();
    if (!gpuRenderer.Initialize(std::move(gpu)))
    {
        std::cerr << "OpenGL context unavailable; GPU regression cannot run.\n";
        return 2;
    }
    SDL_HideWindow(static_cast<SDL_Window *>(glDevice->GetNativeWindow()));
    using DebugCallback = void(APIENTRY *)(decltype(&GlDebug), const void *);
    if (const auto debug = reinterpret_cast<DebugCallback>(SDL_GL_GetProcAddress("glDebugMessageCallback")))
    {
        glEnable(0x92E0);
        glEnable(0x8242);
        debug(GlDebug, nullptr);
    }
    Check(glGetError() == GL_NO_ERROR, "OpenGL initialization does not generate errors");
    Check(glDevice->IsGpuDrivenCullingSupported(), "GPU culling regression uses the compute shader path");
    glDevice->EnableSceneRenderTarget(true, 256, 144);
    glDevice->SetBloomEnabled(false);
    glDevice->SetGtaoEnabled(false);
    glDevice->SetTaaEnabled(false);
    glDevice->SetFxaaEnabled(false);
    World gpuWorld;
    for (float x : {1.4f, 2.6f})
    {
        const auto entity = gpuWorld.CreateEntity("Instance");
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {x, 0, -5};
        gpuWorld.AddComponent(entity, MeshComponent{mesh, material});
    }
    gpuRenderer.SetCameraOverride(true, {}, {0, 90, 0});
    gpuRenderer.RenderFrame(gpuWorld);
    gpuRenderer.SetCameraOverride(true, {2, 0, 0}, {});
    gpuRenderer.RenderFrame(gpuWorld);
    std::vector<unsigned char> pixels(256 * 144 * 4);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int redPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        redPixels += pixels[i] > 180 && pixels[i + 1] < 80 && pixels[i + 2] < 80;
    Check(redPixels > 100, "GPU instances appear on the first frame after a camera turn");
    Check(glGetError() == GL_NO_ERROR, "rendering does not generate OpenGL errors");

    const std::string greenMaterial = (root / "green.mat").generic_string();
    PlatformFile::WriteAllText(greenMaterial, "color 0 1 0 1\nemissive 0 4 0\n");
    gpuWorld.Clear();
    const auto background = gpuWorld.CreateEntity("Background");
    gpuWorld.AddComponent(background, MeshComponent{mesh, greenMaterial});
    gpuWorld.GetComponent<TransformComponent>(background)->position = {0, 0, -6};
    gpuWorld.GetComponent<TransformComponent>(background)->scale = {8, 5, 1};
    const auto foreground = gpuWorld.CreateEntity("Foreground");
    gpuWorld.AddComponent(foreground, MeshComponent{mesh, material});
    gpuWorld.GetComponent<TransformComponent>(foreground)->scale = {0.5f, 3, 1};
    gpuRenderer.SetCameraOverride(true, {}, {});
    gpuWorld.GetComponent<TransformComponent>(foreground)->position = {0, 0, -3};
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::size_t edgePixel = (72 * 256 + 138) * 4;
    const int referenceRed = pixels[edgePixel], referenceGreen = pixels[edgePixel + 1];
    glDevice->SetTaaEnabled(true);
    // 关闭再开启会清空历史；新历史中的柱子盖住下一帧刚露出的绿色背景。
    gpuWorld.GetComponent<TransformComponent>(foreground)->position.x = 0.1f;
    for (int frame = 0; frame < 3; ++frame)
        gpuRenderer.RenderFrame(gpuWorld);
    gpuWorld.GetComponent<TransformComponent>(foreground)->position.x = 0;
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    Check(referenceGreen > 180 && referenceRed < 80 && std::abs(int(pixels[edgePixel]) - referenceRed) < 12 &&
              std::abs(int(pixels[edgePixel + 1]) - referenceGreen) < 12,
          "TAA rejects foreground history on a newly revealed background pixel");

    // 144 高的 HZB 会出现 9 -> 5 的奇数层级；遮挡上下两端但露出中间。
    glDevice->SetTaaEnabled(false);
    gpuWorld.Clear();
    for (float x : {-0.7f, 0.7f})
    {
        const auto entity = gpuWorld.CreateEntity("Partly exposed instance");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, material});
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {x, -1.1518f, -5};
        gpuWorld.GetComponent<TransformComponent>(entity)->scale = {0.5f, 1.276f, 1};
    }
    for (const auto &positionScale : {std::pair<float, float>{-1.282f, 0.970f}, {0.0f, 1.247f}})
    {
        const auto entity = gpuWorld.CreateEntity("Occluding band");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, greenMaterial});
        gpuWorld.GetComponent<TransformComponent>(entity)->position = {0, positionScale.first, -3};
        gpuWorld.GetComponent<TransformComponent>(entity)->scale = {8, positionScale.second, 1};
    }
    const auto countRed = [&]() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int count = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            count += pixels[i] > 180 && pixels[i + 1] < 80 && pixels[i + 2] < 80;
        return count;
    };
    gpuRenderer.SetCameraOverride(true, {0.01f, 0, 0}, {});
    gpuRenderer.RenderFrame(gpuWorld);
    gpuRenderer.SetCameraOverride(true, {}, {});
    gpuRenderer.RenderFrame(gpuWorld);
    const int exposedPixels = countRed();
    gpuRenderer.RenderFrame(gpuWorld);
    Check(exposedPixels > 100 && countRed() >= exposedPixels * 0.95f,
          "stationary HZB retains the exposed middle of instances at rounded mip sizes");
    Check(glGetError() == GL_NO_ERROR, "TAA and HZB history passes do not generate OpenGL errors");
    gpuWorld.Clear();
    const std::string matteMaterial = (root / "matte.mat").generic_string();
    PlatformFile::WriteAllText(matteMaterial, "color 0.08 0.20 0.55 1\nmetallic 0.08\nroughness 0.65\nemissive 0.08 0.08 0.08\n");
    const auto slope = gpuWorld.CreateEntity("Unoccluded tilted plane");
    gpuWorld.AddComponent(slope, MeshComponent{mesh, matteMaterial});
    auto *slopeTransform = gpuWorld.GetComponent<TransformComponent>(slope);
    slopeTransform->position = {0, 0, -5};
    slopeTransform->rotation = {-55, 0, 0};
    slopeTransform->scale = {8, 8, 1};
    gpuRenderer.SetCameraOverride(true, {}, {});
    glDevice->SetGtaoEnabled(false);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto unoccludedReference = pixels;
    glDevice->SetGtaoEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int planeDarkening = 0;
    for (int y = 55; y < 89; ++y)
        for (int x = 105; x < 151; ++x)
        {
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                planeDarkening = std::max(planeDarkening, int(unoccludedReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Tilted plane darkening: " << planeDarkening << '\n';
    Check(planeDarkening <= 3, "GTAO does not treat an unoccluded sloped surface as an occluder");
    glDevice->SetGtaoEnabled(false);
    slopeTransform->rotation = {};
    const auto contactOccluder = gpuWorld.CreateEntity("Nearby contact occluder");
    gpuWorld.AddComponent(contactOccluder, MeshComponent{mesh, matteMaterial});
    // 独立验证环境光遮蔽，避免自发光掩盖正常的接触暗化。
    const auto contactMaterial = (root / "contact.mat").generic_string();
    PlatformFile::WriteAllText(contactMaterial, "color 1 1 1 1\nroughness 1\n");
    gpuWorld.GetComponent<MeshComponent>(slope)->materialPath = contactMaterial;
    gpuWorld.GetComponent<MeshComponent>(contactOccluder)->materialPath = contactMaterial;
    const auto contactLight = gpuWorld.CreateEntity("Contact indirect light only");
    LightComponent contactLighting;
    contactLighting.intensity = 0;
    gpuWorld.AddComponent(contactLight, contactLighting);
    gpuWorld.GetComponent<TransformComponent>(contactOccluder)->position = {0, 0, -4.8f};
    gpuWorld.GetComponent<TransformComponent>(contactOccluder)->scale = {0.5f, 0.5f, 1};
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto contactReference = pixels;
    glDevice->SetGtaoEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int contactDarkening = 0;
    for (int y = 62; y < 82; ++y)
        for (int x = 114; x < 142; ++x)
        {
            if (x >= 120 && x < 136 && y >= 64 && y < 80)
                continue;
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                contactDarkening = std::max(contactDarkening, int(contactReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Contact darkening: " << contactDarkening << '\n';
    Check(contactDarkening >= 3, "GTAO still produces occlusion beside a nearby raised surface");

    // AO 只能遮蔽间接光；近邻遮挡不能改变表面自身发出的光。
    const auto emissionOnly = (root / "emission-only.mat").generic_string();
    PlatformFile::WriteAllText(emissionOnly, "color 0 0 0 1\nemissive 0.3 0.2 0.1\n");
    gpuWorld.GetComponent<MeshComponent>(slope)->materialPath = emissionOnly;
    glDevice->SetGtaoEnabled(false);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto emissionReference = pixels;
    glDevice->SetGtaoEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    int emissionDarkening = 0;
    for (int y = 62; y < 82; ++y)
        for (int x = 114; x < 142; ++x)
        {
            if (x >= 120 && x < 136 && y >= 64 && y < 80)
                continue;
            const auto index = (y * 256 + x) * 4;
            for (int channel = 0; channel < 3; ++channel)
                emissionDarkening = std::max(emissionDarkening, int(emissionReference[index + channel]) - int(pixels[index + channel]));
        }
    std::cout << "Emission darkening: " << emissionDarkening << '\n';
    Check(emissionDarkening <= 1, "GTAO preserves emissive surfaces beside an occluder");

    // 长直墙与平地的接缝沿 X 完全相同，接触遮蔽不应形成竖直亮暗条。
    gpuWorld.Clear();
    const auto diffuseOnly = (root / "diffuse-only.mat").generic_string();
    PlatformFile::WriteAllText(diffuseOnly, "color 0.5 0.6 0.8 1\nroughness 1\n");
    const auto wall = gpuWorld.CreateEntity("Uniform wall");
    gpuWorld.AddComponent(wall, MeshComponent{mesh, diffuseOnly});
    gpuWorld.GetComponent<TransformComponent>(wall)->position = {0, 0, -3};
    gpuWorld.GetComponent<TransformComponent>(wall)->scale = {12, 6, 1};
    const auto floor = gpuWorld.CreateEntity("Uniform floor");
    gpuWorld.AddComponent(floor, MeshComponent{mesh, diffuseOnly});
    gpuWorld.GetComponent<TransformComponent>(floor)->position = {0, -0.5f, -3};
    gpuWorld.GetComponent<TransformComponent>(floor)->rotation = {-90, 0, 0};
    gpuWorld.GetComponent<TransformComponent>(floor)->scale = {12, 12, 1};
    const auto ambientLight = gpuWorld.CreateEntity("Ambient only");
    LightComponent unlit;
    unlit.intensity = 0;
    gpuWorld.AddComponent(ambientLight, unlit);
    glDevice->EnableSceneRenderTarget(true, 800, 450);
    pixels.resize(800 * 450 * 4);
    gpuRenderer.SetCameraOverride(true, {}, {});
    glDevice->SetGtaoEnabled(false);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto seamReference = pixels;
    glDevice->SetGtaoEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
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
    Check(seamVariation <= 2, "GTAO keeps a uniform wall-floor seam free of vertical bands");
    Check(seamContact >= 3, "GTAO retains contact shading on the uniform wall-floor seam");
    glDevice->EnableSceneRenderTarget(true, 256, 144);
    pixels.resize(256 * 144 * 4);
    // 斜面有真实近邻遮挡时，相邻像素不能交替丢失半分辨率的 AO。
    for (bool horizontalSlope : {false, true})
    {
        gpuWorld.Clear();
        const auto tilted = gpuWorld.CreateEntity("Sloped contact receiver");
        gpuWorld.AddComponent(tilted, MeshComponent{mesh, contactMaterial});
        gpuWorld.GetComponent<TransformComponent>(tilted)->position = {0, 0, -5};
        const Vec3 rotation = horizontalSlope ? Vec3{0, 55, 0} : Vec3{-55, 0, 0};
        gpuWorld.GetComponent<TransformComponent>(tilted)->rotation = rotation;
        gpuWorld.GetComponent<TransformComponent>(tilted)->scale = {8, 8, 1};
        const auto band = gpuWorld.CreateEntity("Sloped raised band");
        gpuWorld.AddComponent(band, MeshComponent{mesh, contactMaterial});
        gpuWorld.GetComponent<TransformComponent>(band)->position = {0, 0, -4.8f};
        gpuWorld.GetComponent<TransformComponent>(band)->rotation = rotation;
        gpuWorld.GetComponent<TransformComponent>(band)->scale = horizontalSlope ? Vec3{0.5f, 8, 1} : Vec3{8, 0.5f, 1};
        const auto light = gpuWorld.CreateEntity("Slope indirect light only");
        gpuWorld.AddComponent(light, contactLighting);
        glDevice->SetGtaoEnabled(false);
        gpuRenderer.RenderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        const auto slopeReference = pixels;
        glDevice->SetGtaoEnabled(true);
        gpuRenderer.RenderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int stripeHoles = 0, slopeContact = 0;
        const auto darkeningAt = [&](int coordinate) {
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
        std::cout << " Sloped AO " << horizontalSlope << " holes=" << stripeHoles << " contact=" << slopeContact << '\n';
        Check(stripeHoles == 0 && slopeContact >= 2, "GTAO keeps contact shading on sloped receivers without alternating bands");
    }
    glDevice->SetGtaoEnabled(false);
    gpuWorld.Clear();
    const auto tinyAmbient = (root / "tiny-ambient.mat").generic_string();
    PlatformFile::WriteAllText(tinyAmbient, "color 1 0 0 1\nroughness 1\n");
    const auto farBackground = gpuWorld.CreateEntity("Occluded background behind tiny detail");
    gpuWorld.AddComponent(farBackground, MeshComponent{mesh, contactMaterial});
    gpuWorld.GetComponent<TransformComponent>(farBackground)->position = {0, 0, -5};
    gpuWorld.GetComponent<TransformComponent>(farBackground)->scale = {8, 5, 1};
    const auto backgroundBlock = gpuWorld.CreateEntity("Background contact blocker");
    gpuWorld.AddComponent(backgroundBlock, MeshComponent{mesh, contactMaterial});
    gpuWorld.GetComponent<TransformComponent>(backgroundBlock)->position = {-0.19f, 0, -4.8f};
    gpuWorld.GetComponent<TransformComponent>(backgroundBlock)->scale = {0.2f, 0.5f, 1};
    const auto tinyForeground = gpuWorld.CreateEntity("Isolated narrow diffuse foreground detail");
    gpuWorld.AddComponent(tinyForeground, MeshComponent{mesh, tinyAmbient});
    gpuWorld.GetComponent<TransformComponent>(tinyForeground)->position = {0.012f, 0.012f, -3};
    gpuWorld.GetComponent<TransformComponent>(tinyForeground)->scale = {0.02f, 0.12f, 1};
    const auto tinyLight = gpuWorld.CreateEntity("Tiny detail indirect light only");
    gpuWorld.AddComponent(tinyLight, contactLighting);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto tinyPixel = (72 * 256 + 128) * 4;
    const int tinyReference = pixels[tinyPixel];
    glDevice->SetGtaoEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Tiny diffuse AO: " << tinyReference << " -> " << int(pixels[tinyPixel]) << '\n';
    Check(tinyReference > 60 && tinyReference == int(pixels[tinyPixel]),
          "GTAO does not transfer background occlusion onto an isolated narrow diffuse foreground detail");
    glDevice->SetGtaoEnabled(false);
    gpuWorld.Clear();
    const auto detailBackground = gpuWorld.CreateEntity("Close background");
    gpuWorld.AddComponent(detailBackground, MeshComponent{mesh, greenMaterial});
    gpuWorld.GetComponent<TransformComponent>(detailBackground)->position = {0, 0, -5.03f};
    gpuWorld.GetComponent<TransformComponent>(detailBackground)->scale = {8, 5, 1};
    const auto detail = gpuWorld.CreateEntity("One pixel detail");
    gpuWorld.AddComponent(detail, MeshComponent{mesh, material});
    auto *detailTransform = gpuWorld.GetComponent<TransformComponent>(detail);
    detailTransform->position = {0.02005f, 0.02005f, -5};
    detailTransform->scale = {0.035f, 0.035f, 1};
    glDevice->SetTaaEnabled(false);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto detailPixel = (72 * 256 + 128) * 4;
    const int detailRed = pixels[detailPixel], detailGreen = pixels[detailPixel + 1];
    glDevice->SetTaaEnabled(true);
    detailTransform->position.x += 0.022f;
    for (int frame = 0; frame < 3; ++frame)
        gpuRenderer.RenderFrame(gpuWorld);
    detailTransform->position.x -= 0.022f;
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Detail reference: " << detailRed << ',' << detailGreen << " TAA: " << int(pixels[detailPixel]) << ','
              << int(pixels[detailPixel + 1]) << '\n';
    Check(detailRed > 180 && detailGreen < 80 && std::abs(detailRed - int(pixels[detailPixel])) < 12 &&
              std::abs(detailGreen - int(pixels[detailPixel + 1])) < 12,
          "TAA history filtering preserves a one-pixel detail above a close background");
    glDevice->SetTaaEnabled(false);
    glDevice->SetFxaaEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::cout << "Detail FXAA: " << int(pixels[detailPixel]) << ',' << int(pixels[detailPixel + 1]) << '\n';
    Check(std::abs(detailRed - int(pixels[detailPixel])) < 12 && std::abs(detailGreen - int(pixels[detailPixel + 1])) < 12,
          "FXAA preserves isolated one-pixel geometry instead of blending it into its background");
    glDevice->SetFxaaEnabled(false);
    gpuWorld.Clear();
    const auto jitterCamera = gpuWorld.CreateEntity("Jitter camera");
    gpuWorld.AddComponent(jitterCamera, CameraComponent{});
    for (const auto &part : {std::pair<float, float>{-2.03f, 4.0f}, {0.0f, 0.06f}, {2.03f, 4.0f}})
    {
        const auto entity = gpuWorld.CreateEntity("Sloped striped surface");
        gpuWorld.AddComponent(entity, MeshComponent{mesh, part.first == 0 ? material : greenMaterial});
        auto *transform = gpuWorld.GetComponent<TransformComponent>(entity);
        transform->position = {part.first, 0, -5};
        transform->rotation = {-55, 0, 0};
        transform->scale = {part.second, 8, 1};
    }
    gpuRenderer.SetCameraOverride(false, {}, {});
    std::vector<std::vector<unsigned char>> jitterReferences;
    for (int frame = 0; frame < 8; ++frame)
    {
        glDevice->SetTaaEnabled(false);
        glDevice->SetTaaEnabled(true);
        gpuRenderer.RenderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        jitterReferences.push_back(pixels);
    }
    int accumulatedSlopePixels = 0;
    for (int frame = 0; frame < 8; ++frame)
    {
        gpuRenderer.RenderFrame(gpuWorld);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 55; y < 89; ++y)
            for (int x = 124; x < 132; ++x)
            {
                const auto index = (y * 256 + x) * 4;
                accumulatedSlopePixels += std::abs(int(pixels[index]) - int(jitterReferences[frame][index])) > 12;
            }
    }
    std::cout << "Sloped history contributions: " << accumulatedSlopePixels << '\n';
    Check(accumulatedSlopePixels > 30, "TAA accumulates valid sloped-surface history with real projection jitter");
    glDevice->SetTaaEnabled(false);
    gpuRenderer.SetCameraOverride(true, {}, {});
    gpuWorld.Clear();
    const auto diagonal = gpuWorld.CreateEntity("Diagonal edge");
    gpuWorld.AddComponent(diagonal, MeshComponent{mesh, material});
    auto *diagonalTransform = gpuWorld.GetComponent<TransformComponent>(diagonal);
    diagonalTransform->position = {0, 0, -5};
    diagonalTransform->rotation = {0, 0, 27};
    diagonalTransform->scale = {3, 3, 1};
    const auto countAntialiasedPixels = [&]() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int count = 0;
        for (std::size_t index = 0; index < pixels.size(); index += 4)
            count += pixels[index] > 20 && pixels[index] < 230;
        return count;
    };
    gpuRenderer.RenderFrame(gpuWorld);
    const int aliasedPixels = countAntialiasedPixels();
    glDevice->SetFxaaEnabled(true);
    gpuRenderer.RenderFrame(gpuWorld);
    const int antialiasedPixels = countAntialiasedPixels();
    std::cout << "Diagonal edge intermediate pixels: " << aliasedPixels << " -> " << antialiasedPixels << '\n';
    Check(antialiasedPixels > aliasedPixels + 20, "FXAA still smooths ordinary diagonal edges");
    glDevice->SetFxaaEnabled(false);
    if (argc > 1)
    {
        const auto project = std::filesystem::absolute(argv[1]);
        gpuWorld.Clear();
        resources.Shutdown();
        resources.Initialize(nullptr, project.parent_path() / "assets");
        Check(SceneSerializer::Load(gpuWorld, project.parent_path() / "assets/PacMan.scene"), "load reported PacMan scene");
        glDevice->EnableSceneRenderTarget(true, 800, 450);
        glDevice->SetTaaEnabled(false);
        glDevice->SetBloomEnabled(false);
        glDevice->SetGtaoEnabled(false);
        glDevice->SetFxaaEnabled(false);
        pixels.resize(800 * 450 * 4);
        const auto readImage = [&]() {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(glDevice->GetSceneColorTexture()));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        };
        bool projectStable = true;
        const bool lowView = argc > 2 && std::string(argv[2]) == "low";
        const bool singleDraw = argc > 2 && std::string(argv[2]) == "single";
        const bool faceProbe = argc > 2 && std::string(argv[2]) == "faces";
        const bool aoProbe = argc > 2 && std::string(argv[2]) == "ao";
        for (int frame = 0; frame < (singleDraw || faceProbe ? 48 : 16); ++frame)
        {
            Vec3 position = lowView ? Vec3{7 + frame * 0.025f, 2.5f, 18} : Vec3{7 + frame * 0.025f, 17, 10};
            Vec3 rotation{lowView ? -10.0f : -80.0f, 0, 0};
            if (singleDraw)
            {
                const float angle = DegreesToRadians(frame * 7.5f);
                position = {7 + std::sin(angle) * 12, 5, 7 + std::cos(angle) * 12};
                rotation = {-22.619865f, -frame * 7.5f, 0};
            }
            if (faceProbe)
                position = {7, 4.5f + frame * 0.5f, 6};
            if (aoProbe)
            {
                position = {8.12f + frame * 0.001f, 0.8f, 7.8f};
                rotation = {-40, -8, 0};
            }
            glDevice->SetTaaEnabled(false);
            glDevice->SetGtaoEnabled(false);
            glDevice->SetFxaaEnabled(false);
            gpuRenderer.SetCameraOverride(true, position, rotation);
            gpuRenderer.RenderFrame(gpuWorld);
            const auto reference = readImage();
            if (faceProbe)
            {
                const auto &view = gpuRenderer.GetFrameView();
                const Mat4 vp = view.projection * view.view;
                float clip[4]{};
                for (int axis = 0; axis < 4; ++axis)
                    clip[axis] = vp.At(axis, 0) * 7 + vp.At(axis, 1) + vp.At(axis, 2) * 5 + vp.At(axis, 3);
                const int x = int((clip[0] / clip[3] * 0.5f + 0.5f) * 800), y = int((clip[1] / clip[3] * 0.5f + 0.5f) * 450);
                if (x >= 0 && x < 800 && y >= 0 && y < 450)
                {
                    const int index = (y * 800 + x) * 4;
                    const float depth = -(view.view.At(2, 0) * 7 + view.view.At(2, 1) + view.view.At(2, 2) * 5 + view.view.At(2, 3));
                    std::cout << "Face at depth " << depth << ": " << int(reference[index]) << ',' << int(reference[index + 1]) << ','
                              << int(reference[index + 2]) << '\n';
                }
            }
            if (argc > 3)
            {
                glDevice->SetTaaEnabled(true);
                glDevice->SetGtaoEnabled(std::string(argv[3]) == "all");
                glDevice->SetFxaaEnabled(std::string(argv[3]) == "all");
                gpuRenderer.SetCameraOverride(true, position + Vec3{-0.025f, 0, 0}, rotation);
                for (int warmup = 0; warmup < 3; ++warmup)
                    gpuRenderer.RenderFrame(gpuWorld);
                gpuRenderer.SetCameraOverride(true, position, rotation);
            }
            gpuRenderer.RenderFrame(gpuWorld);
            auto stationary = readImage();
            if (singleDraw)
            {
                auto view = gpuRenderer.GetFrameView();
                view.resetTemporalHistory = true;
                glDevice->BeginFrame(view);
                gpuWorld.GetComponentManager().ForEach<MeshComponent>([&](Entity entity, MeshComponent &component) {
                    RenderItem item;
                    item.mesh = resources.Load<MeshResource>(component.meshPath);
                    item.material = resources.Load<MaterialResource>(component.materialPath);
                    item.model = gpuWorld.GetWorldMatrix(entity);
                    item.previousModel = item.model;
                    glDevice->Draw(item);
                });
                for (auto pass : {PostProcessPass::HierarchicalDepth, PostProcessPass::Gtao, PostProcessPass::Bloom,
                                  PostProcessPass::TemporalResolve, PostProcessPass::ToneMap})
                    glDevice->ExecutePostProcess(pass);
                glDevice->EndFrame();
                stationary = readImage();
            }
            int changed = 0, maximum = 0;
            for (std::size_t index = 0; index < pixels.size(); index += 4)
            {
                int delta = 0;
                for (int channel = 0; channel < 3; ++channel)
                    delta = std::max(delta, std::abs(int(reference[index + channel]) - int(stationary[index + channel])));
                changed += delta > 4;
                maximum = std::max(maximum, delta);
            }
            std::cout << "PacMan frame " << frame << ": changed=" << changed << " max=" << maximum << '\n';
            projectStable = projectStable && changed <= (singleDraw ? 20 : 0);
            if (frame == 8)
            {
                auto save = [&](const std::vector<unsigned char> &data, const char *name) {
                    auto *surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char *>(data.data()), 800, 450, 32, 800 * 4,
                                                                       SDL_PIXELFORMAT_RGBA32);
                    SDL_SaveBMP(surface, (root / name).string().c_str());
                    SDL_FreeSurface(surface);
                };
                save(reference, "pacman-reference.bmp");
                save(stationary, "pacman-stationary.bmp");
                std::cout << "Images: " << root.string() << '\n';
            }
        }
        if (argc <= 3)
            Check(projectStable, "reported PacMan scene retains visible geometry when occlusion history becomes valid");
    }
    gpuRenderer.Shutdown();
    resources.Shutdown();
    return failures ? 1 : 0;
}
