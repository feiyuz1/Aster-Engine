#include "EditorApp.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/Reflection.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Project/ProjectPackager.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace MiniEngine
{
namespace
{
Vec3 TransformPoint(const Mat4 &matrix, const Vec3 &point)
{
    return {matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3),
            matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3),
            matrix.At(2, 0) * point.x + matrix.At(2, 1) * point.y + matrix.At(2, 2) * point.z + matrix.At(2, 3)};
}

bool ProjectToScreen(const FrameView &view, const Vec3 &point, const float minimumX, const float minimumY, const float width,
                     const float height, ImVec2 &screen)
{
    const Mat4 matrix = view.projection * view.view;
    const float x = matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3);
    const float y = matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3);
    const float w = matrix.At(3, 0) * point.x + matrix.At(3, 1) * point.y + matrix.At(3, 2) * point.z + matrix.At(3, 3);
    if (w <= 0.0001f)
        return false;
    screen = {minimumX + (x / w * 0.5f + 0.5f) * width, minimumY + (1.0f - (y / w * 0.5f + 0.5f)) * height};
    return true;
}

bool IntersectRayAabb(const Ray &ray, const Aabb &bounds, float &distance)
{
    float minimumDistance = 0.0f, maximumDistance = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
        const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
        const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
        const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
        if (std::abs(direction) < 1e-6f)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }
        float nearValue = (minimum - origin) / direction, farValue = (maximum - origin) / direction;
        if (nearValue > farValue)
            std::swap(nearValue, farValue);
        minimumDistance = std::max(minimumDistance, nearValue);
        maximumDistance = std::min(maximumDistance, farValue);
        if (minimumDistance > maximumDistance)
            return false;
    }
    distance = minimumDistance;
    return true;
}

Aabb TransformBounds(const Aabb &bounds, const Mat4 &matrix)
{
    const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const Vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const Vec3 worldCenter = TransformPoint(matrix, center);
    const Vec3 worldExtent{
        std::abs(matrix.At(0, 0)) * extent.x + std::abs(matrix.At(0, 1)) * extent.y + std::abs(matrix.At(0, 2)) * extent.z,
        std::abs(matrix.At(1, 0)) * extent.x + std::abs(matrix.At(1, 1)) * extent.y + std::abs(matrix.At(1, 2)) * extent.z,
        std::abs(matrix.At(2, 0)) * extent.x + std::abs(matrix.At(2, 1)) * extent.y + std::abs(matrix.At(2, 2)) * extent.z};
    return {worldCenter - worldExtent, worldCenter + worldExtent};
}

float DistanceToSegment(const ImVec2 point, const ImVec2 start, const ImVec2 end)
{
    const ImVec2 segment{end.x - start.x, end.y - start.y}, offset{point.x - start.x, point.y - start.y};
    const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
    const float t = lengthSquared > 0.0f ? std::clamp((offset.x * segment.x + offset.y * segment.y) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    const float dx = point.x - (start.x + segment.x * t), dy = point.y - (start.y + segment.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

bool InputString(const char *label, std::string &value, const std::size_t capacity = 512)
{
    std::vector<char> buffer(capacity, '\0');
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
    if (!ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        return false;
    value = buffer.data();
    return true;
}
} // namespace

bool EditorApp::Initialize(const std::filesystem::path &projectFile)
{
    if (!m_engine.Initialize(projectFile, false))
        return false;
    m_device = dynamic_cast<SdlGlRenderDevice *>(m_engine.GetContext().renderer->GetDevice());
    if (!m_device)
    {
        m_engine.Shutdown();
        return false;
    }
    if (!m_device->EnableSceneRenderTarget(true, 960, 540))
    {
        m_engine.Shutdown();
        return false;
    }
    m_engine.GetContext().renderer->SetCameraOverride(true, m_editorCameraPosition, m_editorCameraRotation, m_editorCameraFov);
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), "TinyEngine Editor");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window *>(m_device->GetNativeWindow()), m_device->GetGlContext()) ||
        !ImGui_ImplOpenGL3_Init("#version 330"))
    {
        ImGui::DestroyContext();
        m_engine.Shutdown();
        return false;
    }
    m_device->SetEventCallback([](const void *event) { ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event *>(event)); });
    m_engine.GetContext().renderer->SetOverlayCallback([this]() { Draw(); });
    m_messages.emplace_back("Editor initialized. Scene changes are isolated from Play mode.");
    m_sceneDocuments.push_back({m_engine.GetSceneManager().GetCurrentScene(), m_engine.GetSceneManager().CreateSnapshot(), false});
#ifdef TINYENGINE_HAS_ASSIMP
    m_importQueue.Initialize(m_engine.GetContext().jobSystem);
#endif
    if (std::filesystem::is_regular_file(GetAutosavePath()))
        m_messages.emplace_back("Recovery autosave available from File > Restore Autosave.");
    UpdateWindowTitle();
    m_initialized = true;
    return true;
}

void EditorApp::Run(const std::uint32_t maximumFrames)
{
    if (m_initialized)
        m_engine.Run(maximumFrames);
}

void EditorApp::Shutdown()
{
    if (!m_initialized)
        return;
    if (m_sceneDirty && !m_engine.IsPlaying())
        Autosave();
    m_engine.GetContext().renderer->SetOverlayCallback({});
    m_device->SetEventCallback({});
    m_device->EnableSceneRenderTarget(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_engine.Shutdown();
    m_initialized = false;
}

void EditorApp::Draw()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    DrawMenuBar();
    DrawHierarchy();
    DrawInspector();
    DrawSceneView();
    DrawAssets();
    DrawConsole();
    DrawAnimationEditor();
    DrawShaderEditor();
    DrawMaterialGraphEditor();
    DrawEffectEditor();
    DrawBehaviorTreeEditor();
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z))
            Undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y))
            Redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            SaveCurrentScene();
        if (ImGui::IsKeyPressed(ImGuiKey_C) && m_selected != InvalidEntity)
            Prefab::Save(*m_engine.GetContext().world, m_selected, "Saved/EditorClipboard.prefab");
        if (ImGui::IsKeyPressed(ImGuiKey_V))
        {
            RecordUndo();
            const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, "Saved/EditorClipboard.prefab");
            if (!roots.empty())
                m_selected = roots.front();
        }
    }
    if (m_sceneDirty && !m_engine.IsPlaying() && ImGui::GetTime() - m_lastAutosaveTime >= 30.0)
        Autosave();
    UpdateWindowTitle();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorApp::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            SaveCurrentScene();
        if (ImGui::MenuItem("New Scene"))
            NewSceneDocument();
        const auto autosavePath = GetAutosavePath();
        ImGui::BeginDisabled(!std::filesystem::is_regular_file(autosavePath) || m_engine.IsPlaying());
        if (ImGui::MenuItem("Restore Autosave"))
            RestoreAutosave();
        ImGui::EndDisabled();
        const bool canPackage = !m_engine.IsPlaying() && !m_engine.GetProjectSettings().projectFile.empty();
        ImGui::BeginDisabled(!canPackage);
        if (ImGui::MenuItem("Package Project"))
        {
            SaveCurrentScene();
            char *basePath = SDL_GetBasePath();
            const std::filesystem::path executableDirectory = basePath ? std::filesystem::path(basePath) : std::filesystem::current_path();
            if (basePath)
                SDL_free(basePath);
#ifdef _WIN32
            const auto gameExecutable = executableDirectory / "tinyengine_game.exe";
#else
            const auto gameExecutable = executableDirectory / "tinyengine_game";
#endif
            const auto output = m_engine.GetProjectSettings().Resolve(std::filesystem::path("Dist") / m_engine.GetProjectSettings().name);
            const auto result = ProjectPackager::Package({m_engine.GetProjectSettings().projectFile, output, gameExecutable});
            if (result.success)
                m_messages.emplace_back("Packaged project: " + result.outputDirectory.generic_string());
            else
                for (const auto &error : result.errors)
                    m_messages.emplace_back("Package error: " + error);
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty()))
            Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty()))
            Redo();
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (!m_engine.IsPlaying())
    {
        if (ImGui::Button("Play"))
        {
            m_engine.EnterPlayMode();
            m_messages.emplace_back("Play mode started");
        }
    }
    else
    {
        if (ImGui::Button("Stop"))
        {
            m_engine.ExitPlayMode();
            m_selected = InvalidEntity;
            m_messages.emplace_back("Play mode stopped; edit scene restored");
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_engine.IsPlaying() ? "Runtime" : "Editing");
    ImGui::EndMainMenuBar();
}

void EditorApp::DrawHierarchy()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y;
    const float upperHeight = viewport->WorkSize.y * 0.67f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({260.0f, upperHeight}, ImGuiCond_Always);
    ImGui::Begin("Hierarchy");
    World &world = *m_engine.GetContext().world;
    if (!m_engine.IsPlaying() && ImGui::Button("+ Entity"))
    {
        RecordUndo();
        m_selected = world.CreateEntity();
        m_selection = {m_selected};
    }
    std::vector<Entity> entities(world.GetEntityManager().GetAliveEntities().begin(), world.GetEntityManager().GetAliveEntities().end());
    std::sort(entities.begin(), entities.end());
    std::function<void(Entity)> drawNode = [&](const Entity entity) {
        std::vector<Entity> children;
        for (const Entity candidate : entities)
            if (const auto *relation = world.GetComponent<RelationshipComponent>(candidate); relation && relation->parent == entity)
                children.push_back(candidate);
        const auto *name = world.GetComponent<NameComponent>(entity);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (std::find(m_selection.begin(), m_selection.end(), entity) != m_selection.end() || m_selected == entity)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<std::uintptr_t>(entity)), flags, "%s",
                                            name ? name->name.c_str() : "Entity");
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyCtrl)
            {
                const auto selected = std::find(m_selection.begin(), m_selection.end(), entity);
                if (selected == m_selection.end())
                    m_selection.push_back(entity);
                else
                    m_selection.erase(selected);
                m_selected = m_selection.empty() ? InvalidEntity : m_selection.back();
            }
            else
            {
                m_selected = entity;
                m_selection = {entity};
            }
        }
        if (open)
        {
            for (const Entity child : children)
                drawNode(child);
            ImGui::TreePop();
        }
    };
    for (const Entity entity : entities)
    {
        const auto *relation = world.GetComponent<RelationshipComponent>(entity);
        if (!relation || relation->parent == InvalidEntity || !world.IsAlive(relation->parent))
            drawNode(entity);
    }
    if (!m_engine.IsPlaying() && m_selected != InvalidEntity && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        RecordUndo();
        if (m_selection.empty())
            m_selection.push_back(m_selected);
        for (const Entity selected : m_selection)
            if (world.IsAlive(selected))
                world.DestroyEntity(selected);
        m_selection.clear();
        m_selected = InvalidEntity;
    }
    ImGui::End();
}

void EditorApp::DrawInspector()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float width = 350.0f;
    const float upperHeight = viewport->WorkSize.y * 0.67f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - width, viewport->WorkPos.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, upperHeight}, ImGuiCond_Always);
    ImGui::Begin("Inspector");
    if (m_selection.size() > 1)
        ImGui::TextDisabled("%zu entities selected; editing primary", m_selection.size());
    World &world = *m_engine.GetContext().world;
    if (!world.IsAlive(m_selected))
    {
        const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                   : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
        if (asset && asset->type == "Material")
        {
            if (auto material = m_engine.GetContext().resourceManager->Load<MaterialResource>(asset->path))
            {
                ImGui::SeparatorText("Material Asset");
                ImGui::TextWrapped("%s", asset->path.c_str());
                Vec4 color = material->GetBaseColor();
                float metallic = material->GetMetallic(), roughness = material->GetRoughness();
                Vec3 emissive = material->GetEmissive();
                bool transparent = material->IsTransparent();
                bool changed = false;
                std::string parent = material->GetParentPath(), shader = material->GetShaderPath(), albedo = material->GetTexturePath(),
                            normal = material->GetNormalTexturePath(), metallicMap = material->GetMetallicTexturePath();
                std::string roughnessMap = material->GetRoughnessTexturePath(), ao = material->GetAoTexturePath(),
                            emissiveMap = material->GetEmissiveTexturePath(), packed = material->GetPackedTexturePath(),
                            packedLayout = material->GetPackedTextureLayout();
                const bool hasParent = !parent.empty();
                std::unordered_map<std::string, bool> overrides;
                const auto overrideControl = [&](const char *property) {
                    auto found = overrides.find(property);
                    if (found == overrides.end())
                        found = overrides.emplace(property, material->IsOverridden(property)).first;
                    bool &enabled = found->second;
                    if (hasParent)
                    {
                        ImGui::PushID(property);
                        changed = ImGui::Checkbox("##Override", &enabled) || changed;
                        ImGui::SameLine();
                        ImGui::PopID();
                    }
                    return enabled || !hasParent;
                };
                ImGui::BeginDisabled(!overrideControl("color"));
                changed = ImGui::ColorEdit4("Base Color", &color.x) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("metallic"));
                changed = ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("roughness"));
                changed = ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("emissive"));
                changed = ImGui::ColorEdit3("Emissive", &emissive.x) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("blend"));
                changed = ImGui::Checkbox("Transparent", &transparent) || changed;
                ImGui::EndDisabled();
                auto editPath = [&](const char *label, const char *property, std::string &path) {
                    const bool enabled = std::string(property) == "parent" || overrideControl(property);
                    char buffer[512]{};
                    std::strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
                    ImGui::BeginDisabled(!enabled);
                    if (ImGui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        path = buffer;
                        changed = true;
                    }
                    ImGui::EndDisabled();
                };
                editPath("Parent", "parent", parent);
                editPath("Shader", "shader", shader);
                editPath("Albedo", "texture", albedo);
                editPath("Normal", "normalTexture", normal);
                editPath("Metallic Map", "metallicTexture", metallicMap);
                editPath("Roughness Map", "roughnessTexture", roughnessMap);
                editPath("AO Map", "aoTexture", ao);
                editPath("Emissive Map", "emissiveTexture", emissiveMap);
                editPath("Packed ORM/RMA", "packedTexture", packed);
                if (overrideControl("packedLayout"))
                {
                    int layout = packedLayout == "RMA" ? 1 : 0;
                    if (ImGui::Combo("Packed Layout", &layout, "ORM\0RMA\0"))
                    {
                        packedLayout = layout ? "RMA" : "ORM";
                        changed = true;
                    }
                }
                auto parameters = material->GetParameters();
                if (!shader.empty())
                    for (const auto &parameter : ShaderPipeline::ReflectParameters(shader))
                    {
                        Vec4 &value = parameters[parameter.name];
                        if (!material->GetParameters().count(parameter.name))
                            value = {parameter.defaultValue[0], parameter.defaultValue[1], parameter.defaultValue[2],
                                     parameter.defaultValue[3]};
                        const std::string property = "parameter." + parameter.name;
                        ImGui::BeginDisabled(!overrideControl(property.c_str()));
                        bool parameterChanged = false;
                        if (parameter.type == ShaderParameterType::Float)
                            parameterChanged = parameter.hasRange ? ImGui::SliderFloat(parameter.name.c_str(), &value.x, parameter.minimum,
                                                                                       parameter.maximum)
                                                                  : ImGui::DragFloat(parameter.name.c_str(), &value.x, 0.01f);
                        else if (parameter.type == ShaderParameterType::Vec2)
                            parameterChanged = ImGui::DragFloat2(parameter.name.c_str(), &value.x, 0.01f);
                        else if (parameter.type == ShaderParameterType::Vec3)
                            parameterChanged = ImGui::DragFloat3(parameter.name.c_str(), &value.x, 0.01f);
                        else if (parameter.type == ShaderParameterType::Vec4)
                            parameterChanged = ImGui::DragFloat4(parameter.name.c_str(), &value.x, 0.01f);
                        ImGui::EndDisabled();
                        changed = parameterChanged || changed;
                    }
                if (changed)
                {
                    std::ostringstream output;
                    if (!parent.empty())
                        output << "parent " << parent << '\n';
                    if (!parent.empty())
                        for (const auto &entry : overrides)
                            output << "override " << entry.first << ' ' << entry.second << '\n';
                    output << "color " << color.x << ' ' << color.y << ' ' << color.z << ' ' << color.w << "\nmetallic " << metallic
                           << "\nroughness " << roughness << "\nemissive " << emissive.x << ' ' << emissive.y << ' ' << emissive.z << '\n';
                    if (!shader.empty())
                        output << "shader " << shader << '\n';
                    if (!albedo.empty())
                        output << "texture " << albedo << '\n';
                    if (!normal.empty())
                        output << "normalTexture " << normal << '\n';
                    if (!metallicMap.empty())
                        output << "metallicTexture " << metallicMap << '\n';
                    if (!roughnessMap.empty())
                        output << "roughnessTexture " << roughnessMap << '\n';
                    if (!ao.empty())
                        output << "aoTexture " << ao << '\n';
                    if (!emissiveMap.empty())
                        output << "emissiveTexture " << emissiveMap << '\n';
                    if (!packed.empty())
                        output << "packedTexture " << packed << "\npackedLayout " << packedLayout << '\n';
                    for (const auto &parameter : parameters)
                        output << "parameter " << parameter.first << ' ' << parameter.second.x << ' ' << parameter.second.y << ' '
                               << parameter.second.z << ' ' << parameter.second.w << '\n';
                    output << "blend " << (transparent ? "alpha" : "opaque") << '\n';
                    if (PlatformFile::WriteAllText(asset->path, output.str()) && m_engine.GetContext().resourceManager->Reload(asset->path))
                        m_messages.emplace_back("Material saved: " + asset->path);
                    else
                        m_messages.emplace_back("Material save failed: " + asset->path);
                }
            }
        }
        else
            ImGui::TextDisabled("Select an entity or material asset");
        ImGui::End();
        return;
    }
    const bool editable = !m_engine.IsPlaying();
    ImGui::BeginDisabled(!editable);
    const auto &registry = ReflectionRegistry::Get();
    for (const auto &component : registry.GetComponents())
    {
        if (!component.has(world, m_selected))
            continue;
        ImGui::PushID(component.name.c_str());
        ImGui::SeparatorText(component.name.c_str());
        for (const auto &property : component.properties)
        {
            ImGui::PushID(property.name.c_str());
            void *value = property.access ? property.access(world, m_selected) : nullptr;
            bool changed = false;
            switch (property.type)
            {
            case ReflectedPropertyType::Bool: {
                bool edited = value ? *static_cast<bool *>(value) : false;
                changed = value && ImGui::Checkbox(property.name.c_str(), &edited);
                if (changed)
                {
                    RecordUndo();
                    *static_cast<bool *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::UInt: {
                std::uint32_t edited = value ? *static_cast<std::uint32_t *>(value) : 0;
                changed = value && ImGui::InputScalar(property.name.c_str(), ImGuiDataType_U32, &edited);
                if (changed)
                {
                    RecordUndo();
                    *static_cast<std::uint32_t *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::Entity: {
                Entity edited = value ? *static_cast<Entity *>(value) : InvalidEntity;
                changed = value && ImGui::InputScalar(property.name.c_str(), ImGuiDataType_U64, &edited);
                if (changed && (edited == InvalidEntity || world.IsAlive(edited)))
                {
                    RecordUndo();
                    *static_cast<Entity *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::Float: {
                float edited = value ? *static_cast<float *>(value) : 0.0f;
                const float minimum = property.hasRange ? property.minimum : 0.0f;
                const float maximum = property.hasRange ? property.maximum : 0.0f;
                changed = value && ImGui::DragFloat(property.name.c_str(), &edited, property.speed, minimum, maximum);
                if (ImGui::IsItemActivated())
                    RecordUndo();
                if (changed)
                    *static_cast<float *>(value) = edited;
                break;
            }
            case ReflectedPropertyType::Vec3: {
                Vec3 edited = value ? *static_cast<Vec3 *>(value) : Vec3{};
                if (property.color)
                    changed = value && ImGui::ColorEdit3(property.name.c_str(), &edited.x);
                else
                {
                    const float minimum = property.hasRange ? property.minimum : 0.0f;
                    const float maximum = property.hasRange ? property.maximum : 0.0f;
                    changed = value && ImGui::DragFloat3(property.name.c_str(), &edited.x, property.speed, minimum, maximum);
                }
                if (ImGui::IsItemActivated())
                    RecordUndo();
                if (changed)
                    *static_cast<Vec3 *>(value) = edited;
                break;
            }
            case ReflectedPropertyType::String: {
                auto *text = static_cast<std::string *>(value);
                char buffer[512]{};
                if (text)
                    std::strncpy(buffer, text->c_str(), sizeof(buffer) - 1);
                if (text && ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    RecordUndo();
                    *text = buffer;
                    changed = true;
                }
                if (text && !property.assetType.empty() && ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH"))
                    {
                        const std::string path = static_cast<const char *>(payload->Data);
                        const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(path);
                        const bool accepted = asset && (asset->type == property.assetType ||
                                                        (property.assetType == "Animation" && asset->extension == ".anim"));
                        if (accepted)
                        {
                            RecordUndo();
                            *text = path;
                            changed = true;
                        }
                        else
                            m_messages.emplace_back("Inspector rejected " + path + "; expected " + property.assetType);
                    }
                    ImGui::EndDragDropTarget();
                }
                break;
            }
            case ReflectedPropertyType::Enum: {
                int edited = property.getEnum ? property.getEnum(world, m_selected) : 0;
                const char *preview = edited >= 0 && static_cast<std::size_t>(edited) < property.enumNames.size()
                                          ? property.enumNames[edited].c_str()
                                          : "Unknown";
                if (ImGui::BeginCombo(property.name.c_str(), preview))
                {
                    for (std::size_t index = 0; index < property.enumNames.size(); ++index)
                        if (ImGui::Selectable(property.enumNames[index].c_str(), edited == static_cast<int>(index)))
                        {
                            RecordUndo();
                            property.setEnum(world, m_selected, static_cast<int>(index));
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                break;
            }
            }
            (void)changed;
            ImGui::PopID();
        }
        if (component.removable)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
            if (ImGui::SmallButton("Remove Component"))
            {
                RecordUndo();
                component.remove(world, m_selected);
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Component"))
        ImGui::OpenPopup("AddComponentPopup");
    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        for (const auto &component : registry.GetComponents())
            if (component.removable && !component.has(world, m_selected) && ImGui::MenuItem(component.name.c_str()))
            {
                RecordUndo();
                component.add(world, m_selected);
            }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void EditorApp::DrawSceneView()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float left = viewport->WorkPos.x + 260.0f, top = viewport->WorkPos.y;
    const float width = std::max(160.0f, viewport->WorkSize.x - 610.0f), height = viewport->WorkSize.y * 0.67f;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::Begin("Scene View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    DrawSceneTabs();
    if (ImGui::Button("W Translate"))
        m_gizmoMode = GizmoMode::Translate;
    ImGui::SameLine();
    if (ImGui::Button("E Rotate"))
        m_gizmoMode = GizmoMode::Rotate;
    ImGui::SameLine();
    if (ImGui::Button("R Scale"))
        m_gizmoMode = GizmoMode::Scale;
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_gizmoSnap);
    ImGui::SameLine();
    const char *spaceLabel = m_gizmoSpace == GizmoSpace::World ? "World" : "Local";
    if (ImGui::Button(spaceLabel))
        m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
    ImGui::SameLine();
    const char *pivotNames[] = {"Center", "Active", "Individual"};
    int pivot = static_cast<int>(m_pivotMode);
    ImGui::SetNextItemWidth(82.0f);
    if (ImGui::Combo("##Pivot", &pivot, pivotNames, 3))
        m_pivotMode = static_cast<PivotMode>(pivot);
    ImGui::SameLine();
    float *snapValue = m_gizmoMode == GizmoMode::Translate ? &m_translateSnap
                       : m_gizmoMode == GizmoMode::Rotate  ? &m_rotateSnap
                                                           : &m_scaleSnap;
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("##SnapValue", snapValue, 0.05f, 0.001f, 100.0f, "%.2f");
    float exposure = m_device->GetExposure();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.05f, 16.0f, "%.2f"))
        m_device->SetExposure(exposure);
    bool fxaa = m_device->IsFxaaEnabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("FXAA", &fxaa))
        m_device->SetFxaaEnabled(fxaa);
    bool bloom = m_device->IsBloomEnabled(), gtao = m_device->IsGtaoEnabled(), taa = m_device->IsTaaEnabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("Bloom", &bloom))
        m_device->SetBloomEnabled(bloom);
    ImGui::SameLine();
    if (ImGui::Checkbox("GTAO", &gtao))
        m_device->SetGtaoEnabled(gtao);
    ImGui::SameLine();
    if (ImGui::Checkbox("TAA", &taa))
        m_device->SetTaaEnabled(taa);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload Shaders"))
    {
        if (m_device->ReloadShaders())
            m_messages.emplace_back("Shaders reloaded");
        else
            m_messages.emplace_back("Shader reload failed; previous program retained: " + m_device->GetLastShaderError());
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int targetWidth = std::max(1, static_cast<int>(available.x)), targetHeight = std::max(1, static_cast<int>(available.y));
    m_device->ResizeSceneRenderTarget(targetWidth, targetHeight);
    const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_device->GetSceneColorTexture())), available, {0, 1}, {1, 0});
    const bool hovered = ImGui::IsItemHovered();
    if (!m_engine.IsPlaying() && hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            m_gizmoMode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            m_gizmoMode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            m_gizmoMode = GizmoMode::Scale;
    }
    ImGuiIO &io = ImGui::GetIO();
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    const Vec3 right = Normalize(Cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vec3 up = Normalize(Cross(right, forward));
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        m_editorCameraRotation.y += io.MouseDelta.x * 0.18f;
        m_editorCameraRotation.x = std::clamp(m_editorCameraRotation.x - io.MouseDelta.y * 0.18f, -89.0f, 89.0f);
        const float speed = (io.KeyShift ? 12.0f : 4.0f) * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_W))
            m_editorCameraPosition += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S))
            m_editorCameraPosition -= forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D))
            m_editorCameraPosition += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A))
            m_editorCameraPosition -= right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E))
            m_editorCameraPosition += up * speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q))
            m_editorCameraPosition -= up * speed;
    }
    if (hovered && std::abs(io.MouseWheel) > 0.0f)
        m_editorCameraPosition += forward * (io.MouseWheel * 0.8f);
    m_engine.GetContext().renderer->SetCameraOverride(!m_engine.IsPlaying(), m_editorCameraPosition, m_editorCameraRotation,
                                                      m_editorCameraFov);
    const bool gizmoConsumed = !m_engine.IsPlaying() && DrawGizmo(imageMinimum.x, imageMinimum.y, available.x, available.y);
    if (!m_engine.IsPlaying() && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoConsumed)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        m_marqueeActive = true;
        m_marqueeStart = {mouse.x, mouse.y};
        const float normalizedX = (mouse.x - imageMinimum.x) / std::max(1.0f, available.x);
        const float normalizedY = (mouse.y - imageMinimum.y) / std::max(1.0f, available.y);
        const Entity picked = PickEntity(normalizedX, normalizedY);
        if (ImGui::GetIO().KeyCtrl && picked != InvalidEntity)
        {
            const auto selected = std::find(m_selection.begin(), m_selection.end(), picked);
            if (selected == m_selection.end())
                m_selection.push_back(picked);
            else
                m_selection.erase(selected);
            m_selected = m_selection.empty() ? InvalidEntity : m_selection.back();
        }
        else
        {
            m_selected = picked;
            m_selection = picked == InvalidEntity ? std::vector<Entity>{} : std::vector<Entity>{picked};
        }
    }
    if (m_marqueeActive)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 start{m_marqueeStart.x, m_marqueeStart.y};
        const ImVec2 minimum{std::min(start.x, mouse.x), std::min(start.y, mouse.y)};
        const ImVec2 maximum{std::max(start.x, mouse.x), std::max(start.y, mouse.y)};
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (maximum.x - minimum.x > 3.0f || maximum.y - minimum.y > 3.0f))
        {
            ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, IM_COL32(60, 135, 240, 35));
            ImGui::GetWindowDrawList()->AddRect(minimum, maximum, IM_COL32(90, 165, 255, 220));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (maximum.x - minimum.x > 5.0f || maximum.y - minimum.y > 5.0f)
            {
                std::vector<Entity> marquee;
                World &world = *m_engine.GetContext().world;
                for (const Entity entity : world.GetEntityManager().GetAliveEntities())
                {
                    const Mat4 matrix = world.GetWorldMatrix(entity);
                    ImVec2 screen;
                    if (ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)},
                                        imageMinimum.x, imageMinimum.y, available.x, available.y, screen) &&
                        screen.x >= minimum.x && screen.x <= maximum.x && screen.y >= minimum.y && screen.y <= maximum.y)
                        marquee.push_back(entity);
                }
                if (ImGui::GetIO().KeyCtrl)
                    for (const Entity entity : marquee)
                        if (std::find(m_selection.begin(), m_selection.end(), entity) == m_selection.end())
                            m_selection.push_back(entity);
                        else
                            m_selection = std::move(marquee);
                m_selected = m_selection.empty() ? InvalidEntity : m_selection.back();
            }
            m_marqueeActive = false;
        }
    }
    if (!m_engine.IsPlaying() && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH"))
        {
            const std::string path(static_cast<const char *>(payload->Data));
            const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(path);
            if (asset)
            {
                World &world = *m_engine.GetContext().world;
                RecordUndo();
                if (asset->type == "Prefab")
                {
                    const auto roots = Prefab::Instantiate(world, path);
                    if (!roots.empty())
                        m_selected = roots.front();
                }
                else if (asset->type == "Mesh")
                {
                    if (!world.IsAlive(m_selected))
                        m_selected = world.CreateEntity(std::filesystem::path(path).stem().string());
                    if (auto *mesh = world.GetComponent<MeshComponent>(m_selected))
                        mesh->meshPath = path;
                    else
                        world.AddComponent(m_selected, MeshComponent{path, "assets/default.mat"});
                }
                else if (asset->type == "Material" && world.IsAlive(m_selected))
                {
                    if (auto *mesh = world.GetComponent<MeshComponent>(m_selected))
                        mesh->materialPath = path;
                }
                m_messages.emplace_back("Dropped asset: " + path);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

Entity EditorApp::PickEntity(const float normalizedX, const float normalizedY) const
{
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    const Vec3 right = Normalize(Cross(forward, {0.0f, 1.0f, 0.0f})), up = Normalize(Cross(right, forward));
    const float tangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f), aspect = std::max(0.01f, m_device->GetAspectRatio());
    const float x = normalizedX * 2.0f - 1.0f, y = 1.0f - normalizedY * 2.0f;
    const Ray ray{m_editorCameraPosition, Normalize(forward + right * (x * tangent * aspect) + up * (y * tangent))};
    World &world = *m_engine.GetContext().world;
    Entity selected = InvalidEntity;
    float closest = std::numeric_limits<float>::max();
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        if (const auto *active = world.GetComponent<ActiveComponent>(entity); active && !active->active)
            continue;
        Aabb bounds{};
        bool hasBounds = false;
        if (const auto *meshComponent = world.GetComponent<MeshComponent>(entity))
        {
            if (const auto mesh = m_engine.GetContext().resourceManager->Load<MeshResource>(meshComponent->meshPath))
            {
                bounds = TransformBounds(mesh->GetBounds(), world.GetWorldMatrix(entity));
                hasBounds = true;
            }
        }
        else if (const auto *collider = world.GetComponent<ColliderComponent>(entity))
        {
            Vec3 extent =
                collider->shape == ColliderShape::Box
                    ? collider->halfExtents
                    : Vec3{collider->radius,
                           collider->shape == ColliderShape::Capsule ? collider->height * 0.5f + collider->radius : collider->radius,
                           collider->radius};
            bounds = TransformBounds({extent * -1.0f, extent}, world.GetWorldMatrix(entity));
            hasBounds = true;
        }
        float distance = 0.0f;
        if (hasBounds && IntersectRayAabb(ray, bounds, distance) && distance < closest)
        {
            closest = distance;
            selected = entity;
        }
    }
    return selected;
}

bool EditorApp::DrawGizmo(const float minimumX, const float minimumY, const float width, const float height)
{
    World &world = *m_engine.GetContext().world;
    if (!world.IsAlive(m_selected) || !world.HasComponent<TransformComponent>(m_selected))
    {
        m_activeGizmoAxis = -1;
        return false;
    }
    const Mat4 worldMatrix = world.GetWorldMatrix(m_selected);
    Vec3 origin{worldMatrix.At(0, 3), worldMatrix.At(1, 3), worldMatrix.At(2, 3)};
    std::vector<Entity> targets;
    for (const Entity entity : m_selection)
        if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity))
            targets.push_back(entity);
    if (targets.empty())
        targets.push_back(m_selected);
    if (m_pivotMode == PivotMode::Center)
    {
        origin = {};
        for (const Entity entity : targets)
        {
            const Mat4 matrix = world.GetWorldMatrix(entity);
            origin += Vec3{matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
        }
        origin = origin / static_cast<float>(targets.size());
    }
    const float handleLength = std::max(0.5f, Length(origin - m_editorCameraPosition) * 0.13f);
    Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (m_gizmoSpace == GizmoSpace::Local)
    {
        axes[0] = Normalize({worldMatrix.At(0, 0), worldMatrix.At(1, 0), worldMatrix.At(2, 0)});
        axes[1] = Normalize({worldMatrix.At(0, 1), worldMatrix.At(1, 1), worldMatrix.At(2, 1)});
        axes[2] = Normalize({worldMatrix.At(0, 2), worldMatrix.At(1, 2), worldMatrix.At(2, 2)});
    }
    const ImU32 colors[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(70, 220, 100, 255), IM_COL32(75, 135, 245, 255)};
    ImVec2 start;
    if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin, minimumX, minimumY, width, height, start))
        return false;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    bool hovered = false;
    int hoveredAxis = -1;
    for (int axis = 0; axis < 3; ++axis)
    {
        ImVec2 end;
        if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin + axes[axis] * handleLength, minimumX, minimumY, width,
                             height, end))
            continue;
        const bool axisHovered = DistanceToSegment(mouse, start, end) <= 7.0f;
        if (axisHovered)
        {
            hovered = true;
            hoveredAxis = axis;
        }
        drawList->AddLine(start, end, axisHovered || m_activeGizmoAxis == axis ? IM_COL32(255, 225, 80, 255) : colors[axis],
                          axisHovered ? 4.0f : 2.5f);
        drawList->AddCircleFilled(end, 5.0f, colors[axis]);
    }
    if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        RecordUndo();
        m_activeGizmoAxis = hoveredAxis;
        m_gizmoAccumulatedMotion = 0.0f;
        m_gizmoAppliedMotion = 0.0f;
    }
    if (m_activeGizmoAxis >= 0)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_activeGizmoAxis = -1;
        else
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            const float rawMotion = m_gizmoMode == GizmoMode::Translate ? (delta.x - delta.y) * 0.01f * handleLength
                                    : m_gizmoMode == GizmoMode::Rotate  ? (delta.x - delta.y) * 0.5f
                                                                        : (delta.x - delta.y) * 0.01f;
            m_gizmoAccumulatedMotion += rawMotion;
            const float step = m_gizmoMode == GizmoMode::Translate ? m_translateSnap
                               : m_gizmoMode == GizmoMode::Rotate  ? m_rotateSnap
                                                                   : m_scaleSnap;
            const float targetMotion =
                m_gizmoSnap ? std::round(m_gizmoAccumulatedMotion / std::max(0.001f, step)) * step : m_gizmoAccumulatedMotion;
            const float motion = targetMotion - m_gizmoAppliedMotion;
            m_gizmoAppliedMotion = targetMotion;
            for (const Entity entity : targets)
                if (auto *transform = world.GetComponent<TransformComponent>(entity))
                {
                    if (m_gizmoMode == GizmoMode::Translate)
                        transform->position += axes[m_activeGizmoAxis] * motion;
                    else if (m_gizmoMode == GizmoMode::Rotate)
                    {
                        if (m_activeGizmoAxis == 0)
                            transform->rotation.x += motion;
                        else if (m_activeGizmoAxis == 1)
                            transform->rotation.y += motion;
                        else
                            transform->rotation.z += motion;
                        if (m_pivotMode == PivotMode::Center && entity != m_selected)
                        {
                            Vec3 rotation{};
                            if (m_activeGizmoAxis == 0)
                                rotation.x = motion;
                            else if (m_activeGizmoAxis == 1)
                                rotation.y = motion;
                            else
                                rotation.z = motion;
                            transform->position = origin + TransformPoint(RotationMatrix(Quaternion::FromEulerDegrees(rotation)),
                                                                          transform->position - origin);
                        }
                    }
                    else
                    {
                        float *component = m_activeGizmoAxis == 0   ? &transform->scale.x
                                           : m_activeGizmoAxis == 1 ? &transform->scale.y
                                                                    : &transform->scale.z;
                        *component = std::max(0.001f, *component + motion);
                    }
                }
        }
    }
    return hovered || m_activeGizmoAxis >= 0;
}

void EditorApp::DrawAssets()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
    const float halfWidth = viewport->WorkSize.x * 0.58f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
    ImGui::Begin("Assets");
    if (!m_engine.IsPlaying())
    {
        const auto createUniquePath = [this](const std::string &stem, const std::string &extension) {
            const auto root = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory);
            for (int suffix = 0;; ++suffix)
            {
                const auto name = stem + (suffix == 0 ? std::string{} : std::to_string(suffix)) + extension;
                const auto path = root / name;
                if (!std::filesystem::exists(path))
                    return path;
            }
        };
        if (ImGui::Button("+ Blend Tree"))
        {
            BlendTree1D tree;
            tree.samples = {{0.0f, "assets/player.skanim"}};
            const auto path = createUniquePath("NewBlendTree", ".blendtree");
            if (SaveBlendTreeAsset(tree, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Animation Graph"))
        {
            AnimationGraph graph;
            graph.entryState = "Idle";
            graph.parameters = {{"Speed", 0.0f}};
            graph.states = {{"Idle", "assets/player.skanim"}};
            const auto path = createUniquePath("NewAnimationGraph", ".animgraph");
            if (SaveAnimationGraphAsset(graph, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+ VFX"))
        {
            const auto path = createUniquePath("NewEffect", ".vfx");
            if (SaveParticleEffectAsset({}, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Behavior Tree"))
        {
            BehaviorTreeGraph graph;
            graph.root = 1;
            graph.nodes = {{1, "Sequence", "Root", {20.0f, 20.0f}, {2}}, {2, "Action", "Action", {220.0f, 20.0f}, {}}};
            const auto path = createUniquePath("NewBehavior", ".btgraph");
            if (SaveBehaviorTreeGraph(graph, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
            }
        }
        ImGui::Separator();
    }
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask != 0)
    {
        const auto task = m_importQueue.Get(m_activeImportTask);
        ImGui::ProgressBar(task.progress, ImVec2(220.0f, 0.0f),
                           task.status == AssetImportStatus::Queued    ? "Queued"
                           : task.status == AssetImportStatus::Running ? "Importing"
                                                                       : "Finishing");
        if ((task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) && ImGui::Button("Cancel Import"))
            m_importQueue.Cancel(m_activeImportTask);
        if (task.status == AssetImportStatus::Completed || task.status == AssetImportStatus::Failed ||
            task.status == AssetImportStatus::Cancelled)
        {
            if (task.status == AssetImportStatus::Completed)
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(
                    m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
                m_messages.emplace_back("Background model import completed");
            }
            else if (task.status == AssetImportStatus::Cancelled)
                m_messages.emplace_back("Background model import cancelled");
            else
                m_messages.emplace_back("Background model import failed: " + task.result.error);
            for (const auto &warning : task.result.warnings)
                m_messages.emplace_back("Import warning: " + warning);
            m_importQueue.ClearFinished();
            m_activeImportTask = 0;
        }
        ImGui::Separator();
    }
    ImGui::BeginDisabled(m_activeImportTask != 0);
    if (ImGui::Button("Import Model..."))
        ImGui::OpenPopup("ImportModelPopup");
    ImGui::SameLine();
    const auto selectedInfo = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                      : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" || selectedInfo->importSettings.count("source") == 0 ||
                         m_activeImportTask != 0);
    if (ImGui::Button("Reimport"))
    {
        const std::filesystem::path source = selectedInfo->importSettings.at("source");
        ModelImportOptions options;
        if (const auto scale = selectedInfo->importSettings.find("scale"); scale != selectedInfo->importSettings.end())
        {
            try
            {
                options.scale = std::stof(scale->second);
            }
            catch (...)
            {
            }
        }
        if (const auto flip = selectedInfo->importSettings.find("flipUvs"); flip != selectedInfo->importSettings.end())
            options.flipUvs = flip->second == "true";
        if (const auto compress = selectedInfo->importSettings.find("compressAnimations"); compress != selectedInfo->importSettings.end())
            options.compressAnimations = compress->second == "true";
        const auto readFloatSetting = [&selectedInfo](const char *name, float &value) {
            if (const auto setting = selectedInfo->importSettings.find(name); setting != selectedInfo->importSettings.end())
                try
                {
                    value = std::stof(setting->second);
                }
                catch (...)
                {
                }
        };
        readFloatSetting("animationPositionTolerance", options.animationPositionTolerance);
        readFloatSetting("animationRotationToleranceDegrees", options.animationRotationToleranceDegrees);
        readFloatSetting("animationScaleTolerance", options.animationScaleTolerance);
        const std::filesystem::path output = std::filesystem::path(selectedInfo->path).parent_path();
        const auto projectRoot = m_engine.GetProjectSettings().projectFile.empty()
                                     ? std::filesystem::current_path()
                                     : m_engine.GetProjectSettings().projectFile.parent_path();
        options.resourcePrefix = std::filesystem::relative(output, projectRoot);
        m_activeImportTask = m_importQueue.Submit(source, output, options);
        m_messages.emplace_back("Queued model reimport: " + source.generic_string());
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("ImportModelPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static char sourcePath[1024]{};
        static float scale = 1.0f;
        static bool flipUvs = false;
        static bool compressAnimations = true;
        static float positionTolerance = 0.001f, rotationTolerance = 0.1f, scaleTolerance = 0.001f;
        ImGui::InputText("Source", sourcePath, sizeof(sourcePath));
        ImGui::DragFloat("Scale", &scale, 0.01f, 0.0001f, 1000.0f);
        ImGui::Checkbox("Flip UVs", &flipUvs);
        ImGui::Checkbox("Compress Animations", &compressAnimations);
        ImGui::BeginDisabled(!compressAnimations);
        ImGui::DragFloat("Position Error", &positionTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::DragFloat("Rotation Error (deg)", &rotationTolerance, 0.01f, 0.0f, 45.0f, "%.3f");
        ImGui::DragFloat("Scale Error", &scaleTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::EndDisabled();
        if (ImGui::Button("Import") && sourcePath[0] != '\0')
        {
            const std::filesystem::path source(sourcePath);
            const std::string folderName = source.stem().empty() ? "Model" : source.stem().string();
            const auto relativeOutput = m_engine.GetProjectSettings().assetDirectory / "Imported" / folderName;
            const auto output = m_engine.GetProjectSettings().Resolve(relativeOutput);
            ModelImportOptions options;
            options.scale = scale;
            options.flipUvs = flipUvs;
            options.compressAnimations = compressAnimations;
            options.animationPositionTolerance = positionTolerance;
            options.animationRotationToleranceDegrees = rotationTolerance;
            options.animationScaleTolerance = scaleTolerance;
            options.resourcePrefix = relativeOutput;
            m_activeImportTask = m_importQueue.Submit(source, output, options);
            m_messages.emplace_back("Queued model import: " + source.generic_string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();
#endif
    auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
    std::sort(assets.begin(), assets.end(), [](const AssetInfo &a, const AssetInfo &b) { return a.path < b.path; });
    static std::string moveSource;
    static std::array<char, 1024> moveDestination{};
    for (const auto &asset : assets)
    {
        ImGui::PushID(asset.id.c_str());
        ImGui::TextDisabled("%s", asset.type.c_str());
        ImGui::SameLine(90.0f);
        if (ImGui::Selectable(asset.path.c_str(), m_selectedAsset == asset.path))
            m_selectedAsset = asset.path;
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset.path.c_str(), asset.path.size() + 1);
            ImGui::TextUnformatted(asset.path.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("AssetContext"))
        {
            if (ImGui::MenuItem("Move / Rename..."))
            {
                moveSource = asset.path;
                std::fill(moveDestination.begin(), moveDestination.end(), '\0');
                std::strncpy(moveDestination.data(), asset.path.c_str(), moveDestination.size() - 1);
                ImGui::OpenPopup("MoveAssetModal");
            }
            if (ImGui::MenuItem("Regenerate Thumbnail"))
                m_thumbnailAsset.clear();
            ImGui::EndPopup();
        }
        if (!m_engine.IsPlaying() && asset.extension == ".prefab" && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            RecordUndo();
            const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, asset.path);
            if (!roots.empty())
            {
                m_selected = roots.front();
                m_messages.emplace_back("Instantiated prefab: " + asset.path);
            }
        }
        if (!m_engine.IsPlaying() && asset.extension == ".scene" && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            OpenSceneDocument(asset.path);
        ImGui::PopID();
    }
    if (ImGui::BeginPopupModal("MoveAssetModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Move %s", moveSource.c_str());
        ImGui::InputText("Destination", moveDestination.data(), moveDestination.size());
        if (ImGui::Button("Move"))
        {
            std::string error;
            auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
            if (AssetMaintenance::MoveAsset(registry, moveSource, moveDestination.data(), &error))
            {
                m_selectedAsset = moveDestination.data();
                m_messages.emplace_back("Asset moved and references updated");
                ImGui::CloseCurrentPopup();
            }
            else
                m_messages.emplace_back("Move failed: " + error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const auto preview = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                 : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    if (preview)
    {
        ImGui::SeparatorText("Preview and Dependencies");
        if (m_thumbnailAsset != preview->path)
        {
            const std::filesystem::path directory = "Saved/Thumbnails";
            std::filesystem::create_directories(directory);
            m_thumbnailPath = directory / (preview->id + ".ppm");
            std::string error;
            if (AssetMaintenance::GenerateThumbnail(*preview, m_thumbnailPath, 128, &error))
                m_thumbnailAsset = preview->path;
            else
                m_messages.emplace_back("Thumbnail failed: " + error);
        }
        if (!m_thumbnailPath.empty())
        {
            const std::uintptr_t texture = m_device->GetUiTexture(m_thumbnailPath.generic_string());
            if (texture)
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), {96.0f, 96.0f});
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Type: %s", preview->type.c_str());
        ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(preview->byteSize));
        ImGui::Text("Dependencies: %zu", preview->dependencies.size());
        ImGui::EndGroup();
        auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
        for (const auto &issue : AssetMaintenance::FindMissingDependencies(registry))
            if (issue.owner == preview->path)
            {
                ImGui::TextColored({1.0f, 0.45f, 0.3f, 1.0f}, "Missing: %s", issue.missingPath.c_str());
                for (const auto &candidate : issue.candidates)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Repair -> " + candidate).c_str()))
                    {
                        std::string error;
                        if (!AssetMaintenance::RepairDependency(registry, issue.owner, issue.missingPath, candidate, &error))
                            m_messages.emplace_back("Dependency repair failed: " + error);
                    }
                }
            }
    }
    ImGui::End();
}

void EditorApp::DrawAnimationEditor()
{
    if (m_selectedAsset.empty())
        return;
    const std::string extension = std::filesystem::path(m_selectedAsset).extension().string();
    if (extension != ".skanim" && extension != ".blendtree" && extension != ".animgraph")
        return;
    if (m_openAnimationAsset != m_selectedAsset)
    {
        m_openAnimationAsset.clear();
        bool loaded = false;
        if (extension == ".skanim")
            loaded = LoadSkeletalClipAsset(m_selectedAsset, m_editSkeletalClip);
        else if (extension == ".blendtree")
            loaded = LoadBlendTreeAsset(m_selectedAsset, m_editBlendTree);
        else
            loaded = LoadAnimationGraphAsset(m_selectedAsset, m_editAnimationGraph);
        if (!loaded)
        {
            m_messages.emplace_back("Cannot open animation asset: " + m_selectedAsset);
            return;
        }
        m_openAnimationAsset = m_selectedAsset;
    }

    ImGui::SetNextWindowSize({720.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Graph / Events"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", m_openAnimationAsset.c_str());
    ImGui::Separator();
    if (extension == ".skanim")
    {
        ImGui::Text("Clip: %s | %.3f sec | %zu bones", m_editSkeletalClip.name.c_str(), m_editSkeletalClip.duration,
                    m_editSkeletalClip.IsCompressed()      ? m_editSkeletalClip.tracks.size()
                    : m_editSkeletalClip.keyframes.empty() ? 0
                                                           : m_editSkeletalClip.keyframes.front().localPose.size());
        ImGui::Text("Storage: %s", m_editSkeletalClip.IsCompressed() ? "compressed curves" : "raw sampled poses");
        static float debugTime = 0.0f;
        ImGui::SliderFloat("Debug Time", &debugTime, 0.0f, m_editSkeletalClip.duration);
        const auto skeletonPath = std::filesystem::path(m_openAnimationAsset).replace_extension(".skeleton");
        Skeleton debugSkeleton;
        if (LoadSkeletonAsset(skeletonPath, debugSkeleton))
        {
            const auto snapshot =
                BuildAnimationDebugSnapshot(debugSkeleton, m_editSkeletalClip.Sample(debugTime, debugSkeleton.bones.size(), false));
            ImGui::Text("Debug pose: %zu joints / %zu links | Root motion %.2f, %.2f, %.2f", snapshot.jointPositions.size(),
                        snapshot.bones.size(), m_editSkeletalClip.SampleRootMotion(debugTime, false).x,
                        m_editSkeletalClip.SampleRootMotion(debugTime, false).y, m_editSkeletalClip.SampleRootMotion(debugTime, false).z);
        }
        else
            ImGui::TextDisabled("Place a same-name .skeleton beside this clip for joint debug preview");
        ImGui::BeginDisabled(m_editSkeletalClip.IsCompressed());
        if (ImGui::Button("Compress Curves"))
        {
            const auto stats = m_editSkeletalClip.Compress();
            m_messages.emplace_back("Animation curve keys: " + std::to_string(stats.originalCurveKeys) + " -> " +
                                    std::to_string(stats.compressedCurveKeys));
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Events");
        int remove = -1;
        for (std::size_t index = 0; index < m_editSkeletalClip.events.size(); ++index)
        {
            auto &event = m_editSkeletalClip.events[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat("Time", &event.time, 0.01f, 0.0f, m_editSkeletalClip.duration);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            InputString("Name", event.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            InputString("Payload", event.payload, 256);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                remove = static_cast<int>(index);
            ImGui::PopID();
        }
        if (remove >= 0)
            m_editSkeletalClip.events.erase(m_editSkeletalClip.events.begin() + remove);
        if (ImGui::Button("+ Event"))
            m_editSkeletalClip.events.push_back({0.0f, "Event", {}});
        ImGui::SameLine();
        if (ImGui::Button("Save Clip"))
        {
            std::sort(m_editSkeletalClip.events.begin(), m_editSkeletalClip.events.end(),
                      [](const AnimationEvent &left, const AnimationEvent &right) { return left.time < right.time; });
            if (SaveSkeletalClipAsset(m_editSkeletalClip, m_openAnimationAsset))
                m_messages.emplace_back("Saved animation events: " + m_openAnimationAsset);
        }
    }
    else if (extension == ".blendtree")
    {
        InputString("Parameter", m_editBlendTree.parameter, 128);
        ImGui::SeparatorText("1D Samples");
        int remove = -1;
        for (std::size_t index = 0; index < m_editBlendTree.samples.size(); ++index)
        {
            auto &sample = m_editBlendTree.samples[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Threshold", &sample.threshold, 0.05f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(390.0f);
            InputString("Clip", sample.clipPath);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                remove = static_cast<int>(index);
            ImGui::PopID();
        }
        if (remove >= 0)
            m_editBlendTree.samples.erase(m_editBlendTree.samples.begin() + remove);
        if (ImGui::Button("+ Sample"))
            m_editBlendTree.samples.push_back(
                {m_editBlendTree.samples.empty() ? 0.0f : m_editBlendTree.samples.back().threshold + 1.0f, "assets/player.skanim"});
        ImGui::SameLine();
        if (ImGui::Button("Save Blend Tree"))
        {
            std::sort(m_editBlendTree.samples.begin(), m_editBlendTree.samples.end(),
                      [](const BlendTreeSample &left, const BlendTreeSample &right) { return left.threshold < right.threshold; });
            if (SaveBlendTreeAsset(m_editBlendTree, m_openAnimationAsset))
                m_messages.emplace_back("Saved blend tree: " + m_openAnimationAsset);
            else
                m_messages.emplace_back("Blend tree requires unique thresholds and valid clip paths");
        }
    }
    else
    {
        InputString("Entry State", m_editAnimationGraph.entryState, 128);
        ImGui::SeparatorText("Graph Preview");
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float nodeWidth = 120.0f, spacing = 160.0f, nodeHeight = 52.0f;
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        for (const auto &transition : m_editAnimationGraph.transitions)
        {
            const auto from = std::find_if(m_editAnimationGraph.states.begin(), m_editAnimationGraph.states.end(),
                                           [&transition](const AnimationGraphState &state) { return state.name == transition.from; });
            const auto to = std::find_if(m_editAnimationGraph.states.begin(), m_editAnimationGraph.states.end(),
                                         [&transition](const AnimationGraphState &state) { return state.name == transition.to; });
            if (from != m_editAnimationGraph.states.end() && to != m_editAnimationGraph.states.end())
            {
                const float fromX = origin.x + static_cast<float>(from - m_editAnimationGraph.states.begin()) * spacing + nodeWidth;
                const float toX = origin.x + static_cast<float>(to - m_editAnimationGraph.states.begin()) * spacing;
                drawList->AddLine({fromX, origin.y + nodeHeight * 0.5f}, {toX, origin.y + nodeHeight * 0.5f}, IM_COL32(255, 190, 70, 255),
                                  2.0f);
            }
        }
        for (std::size_t index = 0; index < m_editAnimationGraph.states.size(); ++index)
        {
            const auto &state = m_editAnimationGraph.states[index];
            const ImVec2 minimum{origin.x + static_cast<float>(index) * spacing, origin.y};
            const ImVec2 maximum{minimum.x + nodeWidth, minimum.y + nodeHeight};
            drawList->AddRectFilled(minimum, maximum,
                                    state.name == m_editAnimationGraph.entryState ? IM_COL32(50, 125, 80, 255) : IM_COL32(55, 65, 80, 255),
                                    6.0f);
            drawList->AddRect(minimum, maximum, IM_COL32(150, 170, 200, 255), 6.0f);
            drawList->AddText({minimum.x + 8.0f, minimum.y + 8.0f}, IM_COL32_WHITE, state.name.c_str());
            drawList->AddText({minimum.x + 8.0f, minimum.y + 28.0f}, IM_COL32(180, 190, 205, 255),
                              std::filesystem::path(state.motionPath).filename().string().c_str());
        }
        ImGui::Dummy({std::max(1.0f, static_cast<float>(m_editAnimationGraph.states.size()) * spacing), nodeHeight + 10.0f});

        ImGui::SeparatorText("Parameters");
        int removeParameter = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.parameters.size(); ++index)
        {
            auto &parameter = m_editAnimationGraph.parameters[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(220.0f);
            InputString("Name", parameter.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Default", &parameter.defaultValue, 0.05f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeParameter = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeParameter >= 0)
            m_editAnimationGraph.parameters.erase(m_editAnimationGraph.parameters.begin() + removeParameter);
        if (ImGui::Button("+ Parameter"))
            m_editAnimationGraph.parameters.push_back({"Parameter" + std::to_string(m_editAnimationGraph.parameters.size()), 0.0f});

        ImGui::SeparatorText("States");
        int removeState = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.states.size(); ++index)
        {
            auto &state = m_editAnimationGraph.states[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(180.0f);
            InputString("State", state.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(360.0f);
            InputString("Motion", state.motionPath);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeState = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeState >= 0)
            m_editAnimationGraph.states.erase(m_editAnimationGraph.states.begin() + removeState);
        if (ImGui::Button("+ State"))
            m_editAnimationGraph.states.push_back({"State" + std::to_string(m_editAnimationGraph.states.size()), "assets/player.skanim"});

        ImGui::SeparatorText("Transitions");
        static const char *comparisonNames[] = {"<", "<=", ">", ">=", "==", "!="};
        int removeTransition = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.transitions.size(); ++index)
        {
            auto &transition = m_editAnimationGraph.transitions[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(100.0f);
            InputString("From", transition.from, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            InputString("To", transition.to, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            InputString("Param", transition.parameter, 128);
            ImGui::SameLine();
            int comparison = static_cast<int>(transition.comparison);
            ImGui::SetNextItemWidth(55.0f);
            if (ImGui::Combo("Op", &comparison, comparisonNames, 6))
                transition.comparison = static_cast<AnimationGraphComparison>(comparison);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat("Value", &transition.threshold, 0.05f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat("Blend", &transition.blendSeconds, 0.01f, 0.0f, 10.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeTransition = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeTransition >= 0)
            m_editAnimationGraph.transitions.erase(m_editAnimationGraph.transitions.begin() + removeTransition);
        if (ImGui::Button("+ Transition") && !m_editAnimationGraph.states.empty() && !m_editAnimationGraph.parameters.empty())
            m_editAnimationGraph.transitions.push_back({m_editAnimationGraph.states.front().name, m_editAnimationGraph.states.back().name,
                                                        m_editAnimationGraph.parameters.front().name, AnimationGraphComparison::Greater,
                                                        0.5f, 0.15f});
        ImGui::SameLine();
        if (ImGui::Button("Save Animation Graph"))
        {
            if (SaveAnimationGraphAsset(m_editAnimationGraph, m_openAnimationAsset))
                m_messages.emplace_back("Saved animation graph: " + m_openAnimationAsset);
            else
                m_messages.emplace_back("Animation graph has invalid states, parameters, entry, or transitions");
        }
    }
    ImGui::End();
}

void EditorApp::DrawEffectEditor()
{
    if (m_selectedAsset.empty() || std::filesystem::path(m_selectedAsset).extension() != ".vfx")
        return;
    if (m_openEffectAsset != m_selectedAsset)
    {
        if (!LoadParticleEffectAsset(m_selectedAsset, m_editEffect))
        {
            m_messages.emplace_back("Cannot open VFX asset: " + m_selectedAsset);
            return;
        }
        m_openEffectAsset = m_selectedAsset;
    }
    ImGui::SetNextWindowSize({520.0f, 580.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VFX Editor"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", m_openEffectAsset.c_str());
    int capacity = static_cast<int>(std::min<std::size_t>(m_editEffect.maximumParticles, 1'000'000));
    if (ImGui::DragInt("Capacity", &capacity, 16.0f, 1, 1'000'000))
        m_editEffect.maximumParticles = static_cast<std::size_t>(capacity);
    ImGui::DragFloat("Emission Rate", &m_editEffect.emissionRate, 0.25f, 0.0f, 100000.0f);
    ImGui::DragFloat("Lifetime", &m_editEffect.lifetime, 0.01f, 0.01f, 600.0f);
    ImGui::DragFloat("Speed", &m_editEffect.speed, 0.05f, 0.0f, 1000.0f);
    ImGui::DragFloat("Spread", &m_editEffect.spread, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat3("Gravity", &m_editEffect.gravity.x, 0.05f);
    ImGui::DragFloat2("Size Start / End", &m_editEffect.startSize, 0.01f, 0.0f, 100.0f);
    ImGui::ColorEdit4("Start Color", &m_editEffect.startColor.x);
    ImGui::ColorEdit4("End Color", &m_editEffect.endColor.x);
    InputString("Texture", m_editEffect.texturePath);
    int columns = m_editEffect.flipbookColumns, rows = m_editEffect.flipbookRows;
    if (ImGui::DragInt("Flipbook Columns", &columns, 1.0f, 1, 256))
        m_editEffect.flipbookColumns = static_cast<std::uint16_t>(columns);
    if (ImGui::DragInt("Flipbook Rows", &rows, 1.0f, 1, 256))
        m_editEffect.flipbookRows = static_cast<std::uint16_t>(rows);
    ImGui::DragFloat("Frames / Second", &m_editEffect.flipbookFramesPerSecond, 0.1f, 0.0f, 1000.0f);
    ImGui::Checkbox("Loop Flipbook", &m_editEffect.flipbookLooping);
    int mode = static_cast<int>(m_editEffect.simulationMode);
    if (ImGui::Combo("Simulation", &mode, "CPU\0GPU\0"))
        m_editEffect.simulationMode = static_cast<ParticleSimulationMode>(mode);
    if (ImGui::Button("Save Effect"))
    {
        if (SaveParticleEffectAsset(m_editEffect, m_openEffectAsset))
            m_messages.emplace_back("Saved VFX effect: " + m_openEffectAsset);
        else
            m_messages.emplace_back("VFX effect contains invalid capacity, lifetime, or flipbook dimensions");
    }
    ImGui::End();
}

void EditorApp::DrawBehaviorTreeEditor()
{
    if (m_selectedAsset.empty() || std::filesystem::path(m_selectedAsset).extension() != ".btgraph")
        return;
    if (m_openBehaviorAsset != m_selectedAsset)
    {
        if (!LoadBehaviorTreeGraph(m_selectedAsset, m_editBehaviorGraph))
        {
            m_messages.emplace_back("Cannot open behavior tree: " + m_selectedAsset);
            return;
        }
        m_openBehaviorAsset = m_selectedAsset;
    }
    ImGui::SetNextWindowSize({760.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Behavior Tree Editor"))
    {
        ImGui::End();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    for (const auto &node : m_editBehaviorGraph.nodes)
        for (const auto childId : node.children)
            if (const auto *child = m_editBehaviorGraph.Find(childId))
                drawList->AddLine({origin.x + node.editorPosition.x + 130.0f, origin.y + node.editorPosition.y + 25.0f},
                                  {origin.x + child->editorPosition.x, origin.y + child->editorPosition.y + 25.0f},
                                  IM_COL32(220, 180, 70, 255), 2.0f);
    for (auto &node : m_editBehaviorGraph.nodes)
    {
        ImGui::SetCursorScreenPos({origin.x + node.editorPosition.x, origin.y + node.editorPosition.y});
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Button((node.label + "\n" + node.type).c_str(), {130.0f, 50.0f});
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            node.editorPosition.x += ImGui::GetIO().MouseDelta.x;
            node.editorPosition.y += ImGui::GetIO().MouseDelta.y;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + 340.0f});
    ImGui::Dummy({700.0f, 1.0f});
    ImGui::SeparatorText("Nodes");
    int remove = -1;
    for (std::size_t index = 0; index < m_editBehaviorGraph.nodes.size(); ++index)
    {
        auto &node = m_editBehaviorGraph.nodes[index];
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Text("#%u", node.id);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        InputString("Type", node.type, 64);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        InputString("Label", node.label, 128);
        ImGui::SameLine();
        if (node.id != m_editBehaviorGraph.root && ImGui::SmallButton("X"))
            remove = static_cast<int>(index);
        ImGui::PopID();
    }
    if (remove >= 0)
    {
        const std::uint32_t removedId = m_editBehaviorGraph.nodes[remove].id;
        m_editBehaviorGraph.nodes.erase(m_editBehaviorGraph.nodes.begin() + remove);
        for (auto &node : m_editBehaviorGraph.nodes)
            node.children.erase(std::remove(node.children.begin(), node.children.end(), removedId), node.children.end());
    }
    if (ImGui::Button("+ Action Node"))
    {
        std::uint32_t id = 1;
        for (const auto &node : m_editBehaviorGraph.nodes)
            id = std::max(id, node.id + 1);
        m_editBehaviorGraph.nodes.push_back({id, "Action", "Action", {220.0f, 80.0f + id * 35.0f}, {}});
        if (auto root = std::find_if(m_editBehaviorGraph.nodes.begin(), m_editBehaviorGraph.nodes.end(),
                                     [this](const BehaviorGraphNode &node) { return node.id == m_editBehaviorGraph.root; });
            root != m_editBehaviorGraph.nodes.end())
            root->children.push_back(id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Behavior Tree"))
    {
        if (SaveBehaviorTreeGraph(m_editBehaviorGraph, m_openBehaviorAsset))
            m_messages.emplace_back("Saved behavior tree: " + m_openBehaviorAsset);
        else
            m_messages.emplace_back("Behavior tree contains a cycle, invalid root, or missing child");
    }
    ImGui::End();
}

void EditorApp::DrawShaderEditor()
{
    const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                               : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    if (!asset || asset->type != "Shader")
        return;
    if (m_openShaderAsset != asset->path)
    {
        const std::string source = PlatformFile::ReadAllText(asset->path);
        m_shaderSource.assign(std::max<std::size_t>(source.size() + 65536, 65536), '\0');
        std::copy(source.begin(), source.end(), m_shaderSource.begin());
        m_openShaderAsset = asset->path;
    }
    ImGui::SetNextWindowSize({760.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shader Source Editor"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(asset->path.c_str());
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
    const bool edited = ImGui::InputTextMultiline("##ShaderSource", m_shaderSource.data(), m_shaderSource.size(), {-1.0f, -72.0f}, flags);
    if (edited)
    {
        PlatformFile::WriteAllText(asset->path, m_shaderSource.data());
        m_device->ValidateShader(asset->path);
    }
    if (ImGui::Button("Save and Reload"))
    {
        PlatformFile::WriteAllText(asset->path, m_shaderSource.data());
        if (m_device->ValidateShader(asset->path) && m_device->ReloadShaders())
            m_messages.emplace_back("Shader saved and reloaded: " + asset->path);
        else
            m_messages.emplace_back("Shader error: " + m_device->GetLastShaderError());
    }
    ImGui::SameLine();
    if (m_device->GetLastShaderError().empty())
        ImGui::TextColored({0.35f, 0.9f, 0.45f, 1.0f}, "No compile errors");
    else
    {
        int line = 0;
        const std::string &error = m_device->GetLastShaderError();
        const std::size_t begin = error.find('('), end = begin == std::string::npos ? std::string::npos : error.find(')', begin);
        if (begin != std::string::npos && end != std::string::npos)
            try
            {
                line = std::stoi(error.substr(begin + 1, end - begin - 1));
            }
            catch (...)
            {
            }
        if (line > 0)
            ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "Line %d: %s", line, error.c_str());
        else
            ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s", error.c_str());
    }
    ImGui::End();
}

void EditorApp::DrawMaterialGraphEditor()
{
    const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                               : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    if (!asset || asset->type != "MaterialGraph")
        return;
    if (m_openMaterialGraphAsset != asset->path)
    {
        m_materialGraph.Load(asset->path);
        m_openMaterialGraphAsset = asset->path;
    }
    ImGui::SetNextWindowSize({820.0f, 560.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Graph");
    if (ImGui::Button("+ Constant"))
        m_materialGraph.AddNode(MaterialNodeType::Constant, "Constant", {30.0f, 60.0f});
    ImGui::SameLine();
    if (ImGui::Button("+ Texture"))
        m_materialGraph.AddNode(MaterialNodeType::Texture, "Texture", {30.0f, 190.0f});
    ImGui::SameLine();
    if (ImGui::Button("+ Multiply"))
        m_materialGraph.AddNode(MaterialNodeType::Multiply, "Multiply", {280.0f, 100.0f});
    ImGui::SameLine();
    if (ImGui::Button("+ Output"))
        m_materialGraph.AddNode(MaterialNodeType::PbrOutput, "PBR Output", {540.0f, 100.0f});
    ImGui::SameLine();
    if (ImGui::Button("Save + Compile"))
    {
        std::vector<std::string> errors;
        const std::string shader = m_materialGraph.Compile(&errors);
        const std::filesystem::path output = std::filesystem::path(asset->path).replace_extension(".frag");
        if (m_materialGraph.Save(asset->path) && !shader.empty() && PlatformFile::WriteAllText(output, shader))
            m_messages.emplace_back("Material graph compiled: " + output.generic_string());
        for (const auto &error : errors)
            m_messages.emplace_back("Material graph: " + error);
    }
    ImGui::Separator();
    const ImVec2 canvas = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    static std::uint32_t linkFrom = 0;
    auto nodeCenter = [&](const MaterialNode &node) {
        return ImVec2{canvas.x + node.position.x + 70.0f, canvas.y + node.position.y + 38.0f};
    };
    for (const auto &link : m_materialGraph.GetLinks())
    {
        const auto from = std::find_if(m_materialGraph.GetNodes().begin(), m_materialGraph.GetNodes().end(),
                                       [&](const auto &node) { return node.id == link.fromNode; });
        const auto to = std::find_if(m_materialGraph.GetNodes().begin(), m_materialGraph.GetNodes().end(),
                                     [&](const auto &node) { return node.id == link.toNode; });
        if (from != m_materialGraph.GetNodes().end() && to != m_materialGraph.GetNodes().end())
            drawList->AddBezierCubic(nodeCenter(*from), {nodeCenter(*from).x + 60.0f, nodeCenter(*from).y},
                                     {nodeCenter(*to).x - 60.0f, nodeCenter(*to).y}, nodeCenter(*to), IM_COL32(120, 190, 255, 255), 2.0f);
    }
    for (auto &node : m_materialGraph.GetNodes())
    {
        ImGui::SetCursorScreenPos({canvas.x + node.position.x, canvas.y + node.position.y});
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::InvisibleButton("node", {140.0f, 76.0f});
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyShift)
        {
            node.position.x += ImGui::GetIO().MouseDelta.x;
            node.position.y += ImGui::GetIO().MouseDelta.y;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyShift)
        {
            if (linkFrom == 0)
                linkFrom = node.id;
            else
            {
                m_materialGraph.Connect(linkFrom, node.id, 0);
                linkFrom = 0;
            }
        }
        const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
        drawList->AddRectFilled(minimum, maximum, hovered ? IM_COL32(58, 78, 112, 255) : IM_COL32(42, 52, 72, 255), 6.0f);
        drawList->AddRect(minimum, maximum, linkFrom == node.id ? IM_COL32(255, 205, 70, 255) : IM_COL32(90, 125, 170, 255), 6.0f);
        drawList->AddText({minimum.x + 8.0f, minimum.y + 8.0f}, IM_COL32_WHITE, node.name.c_str());
        drawList->AddText({minimum.x + 8.0f, minimum.y + 34.0f}, IM_COL32(175, 190, 210, 255), "Shift-click: link");
        ImGui::PopID();
    }
    ImGui::Dummy({760.0f, 430.0f});
    ImGui::End();
}

void EditorApp::DrawConsole()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
    const float left = viewport->WorkPos.x + viewport->WorkSize.x * 0.58f;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({viewport->WorkPos.x + viewport->WorkSize.x - left, viewport->WorkPos.y + viewport->WorkSize.y - top},
                             ImGuiCond_Always);
    ImGui::Begin("Console / Stats");
    const Renderer *renderer = m_engine.GetContext().renderer;
    ImGui::Text("Draw calls: %zu | Culled: %zu | LOD: %zu | GPU: %.2f ms", renderer->GetLastDrawCallCount(), renderer->GetLastCulledCount(),
                renderer->GetLastLodSwitchCount(), m_device->GetLastGpuFrameMilliseconds());
    const auto &profile = renderer->GetLastFrameProfile();
    ImGui::Text("CPU: %.2f ms (queue %.2f | shadow %.2f | scene/UI %.2f)", profile.totalMilliseconds, profile.queueMilliseconds,
                profile.shadowMilliseconds, profile.sceneMilliseconds);
    ImGui::Text("Render Graph: %zu passes / %zu physical resources | Cluster refs: %zu | HZB: %zu levels | GPU Driven: %s",
                profile.graphPasses, profile.graphPhysicalResources, profile.clusteredLightReferences, m_device->GetHzbLevelCount(),
                m_device->IsGpuDrivenCullingSupported() ? "Compute + Indirect"
                                                        : (m_device->IsIndirectDrawSupported() ? "CPU cull + Indirect" : "fallback"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##ConsoleCommand", m_consoleInput.data(), m_consoleInput.size(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const std::string command = m_consoleInput.data();
        if (!command.empty() && m_engine.GetContext().diagnostics)
        {
            m_messages.push_back("> " + command);
            const std::string response = m_engine.GetContext().diagnostics->GetConsole().Execute(command);
            if (!response.empty())
                m_messages.push_back(response);
        }
        m_consoleInput.fill('\0');
        ImGui::SetKeyboardFocusHere(-1);
    }
    for (const auto &message : m_messages)
        ImGui::TextUnformatted(message.c_str());
    ImGui::End();
}

void EditorApp::DrawSceneTabs()
{
    if (!ImGui::BeginTabBar("SceneDocuments"))
        return;
    for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
    {
        const auto &document = m_sceneDocuments[index];
        const std::string label =
            (document.path.empty() ? "Untitled" : document.path.filename().string()) + "##Scene" + std::to_string(index);
        ImGuiTabItemFlags flags =
            (index == m_activeSceneDocument ? m_sceneDirty : document.dirty) ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(label.c_str(), nullptr, flags))
        {
            if (index != m_activeSceneDocument)
                SwitchSceneDocument(index);
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

bool EditorApp::SwitchSceneDocument(const std::size_t index)
{
    if (m_engine.IsPlaying() || index >= m_sceneDocuments.size() || index == m_activeSceneDocument)
        return false;
    auto &current = m_sceneDocuments[m_activeSceneDocument];
    current.snapshot = m_engine.GetSceneManager().CreateSnapshot();
    current.dirty = m_sceneDirty;
    if (!m_engine.GetSceneManager().RestoreSnapshot(m_sceneDocuments[index].snapshot))
        return false;
    m_activeSceneDocument = index;
    m_sceneDirty = m_sceneDocuments[index].dirty;
    m_selected = InvalidEntity;
    m_undo.clear();
    m_redo.clear();
    return true;
}

bool EditorApp::OpenSceneDocument(const std::filesystem::path &path)
{
    const auto normalized = std::filesystem::path(path).lexically_normal();
    for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
        if (m_sceneDocuments[index].path.lexically_normal() == normalized)
            return index == m_activeSceneDocument || SwitchSceneDocument(index);
    auto &current = m_sceneDocuments[m_activeSceneDocument];
    current.snapshot = m_engine.GetSceneManager().CreateSnapshot();
    current.dirty = m_sceneDirty;
    if (!SceneSerializer::Load(*m_engine.GetContext().world, normalized))
    {
        m_engine.GetSceneManager().RestoreSnapshot(current.snapshot);
        m_messages.emplace_back("Scene open failed: " + normalized.generic_string());
        return false;
    }
    m_sceneDocuments.push_back({normalized, m_engine.GetSceneManager().CreateSnapshot(), false});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_sceneDirty = false;
    m_selected = InvalidEntity;
    m_undo.clear();
    m_redo.clear();
    m_messages.emplace_back("Opened scene tab: " + normalized.generic_string());
    return true;
}

void EditorApp::NewSceneDocument()
{
    if (m_engine.IsPlaying())
        return;
    auto &current = m_sceneDocuments[m_activeSceneDocument];
    current.snapshot = m_engine.GetSceneManager().CreateSnapshot();
    current.dirty = m_sceneDirty;
    m_engine.GetSceneManager().NewScene();
    m_sceneDocuments.push_back({{}, m_engine.GetSceneManager().CreateSnapshot(), true});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_sceneDirty = true;
    m_selected = InvalidEntity;
    m_undo.clear();
    m_redo.clear();
}

void EditorApp::RecordUndo()
{
    if (m_engine.IsPlaying())
        return;
    m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot());
    if (m_undo.size() > 64)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_sceneDirty = true;
}
void EditorApp::Undo()
{
    if (m_engine.IsPlaying() || m_undo.empty())
        return;
    m_redo.push_back(m_engine.GetSceneManager().CreateSnapshot());
    m_engine.GetSceneManager().RestoreSnapshot(m_undo.back());
    m_undo.pop_back();
    m_selected = InvalidEntity;
    m_sceneDirty = true;
}
void EditorApp::Redo()
{
    if (m_engine.IsPlaying() || m_redo.empty())
        return;
    m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot());
    m_engine.GetSceneManager().RestoreSnapshot(m_redo.back());
    m_redo.pop_back();
    m_selected = InvalidEntity;
    m_sceneDirty = true;
}
bool EditorApp::SaveCurrentScene()
{
    auto path = m_activeSceneDocument < m_sceneDocuments.size() ? m_sceneDocuments[m_activeSceneDocument].path : std::filesystem::path{};
    if (path.empty())
        path = m_engine.GetSceneManager().GetCurrentScene();
    if (path.empty())
        path = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().startupScene);
    const bool saved = m_engine.SaveScene(path);
    if (saved)
    {
        m_sceneDirty = false;
        if (m_activeSceneDocument < m_sceneDocuments.size())
        {
            auto &document = m_sceneDocuments[m_activeSceneDocument];
            document.path = path;
            document.snapshot = m_engine.GetSceneManager().CreateSnapshot();
            document.dirty = false;
        }
        std::error_code error;
        std::filesystem::remove(GetAutosavePath(), error);
    }
    m_messages.emplace_back(saved ? "Scene saved: " + path.generic_string() : "Scene save failed");
    return saved;
}

std::filesystem::path EditorApp::GetAutosavePath()
{
    return m_engine.GetProjectSettings().Resolve("Saved/EditorAutosave.scene");
}

bool EditorApp::Autosave()
{
    m_lastAutosaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    const auto path = GetAutosavePath();
    const bool saved = SceneSerializer::Save(*m_engine.GetContext().world, path);
    if (saved)
        m_messages.emplace_back("Autosaved recovery scene: " + path.generic_string());
    else
        m_messages.emplace_back("Autosave failed: " + path.generic_string());
    return saved;
}

bool EditorApp::RestoreAutosave()
{
    if (m_engine.IsPlaying() || !std::filesystem::is_regular_file(GetAutosavePath()))
        return false;
    RecordUndo();
    const bool loaded = SceneSerializer::Load(*m_engine.GetContext().world, GetAutosavePath());
    if (loaded)
    {
        m_selected = InvalidEntity;
        m_sceneDirty = true;
    }
    m_messages.emplace_back(loaded ? "Restored editor autosave" : "Failed to restore editor autosave");
    return loaded;
}

void EditorApp::UpdateWindowTitle()
{
    if (!m_device || (m_titleInitialized && m_lastTitleDirty == m_sceneDirty))
        return;
    const std::string title = "TinyEngine Editor - " + m_engine.GetProjectSettings().name + (m_sceneDirty ? " *" : "");
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), title.c_str());
    m_lastTitleDirty = m_sceneDirty;
    m_titleInitialized = true;
}
} // namespace MiniEngine
