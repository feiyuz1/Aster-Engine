// 文件：editor/EditorApp.cpp
// 作用：定义编辑器窗口、面板状态、选择、Gizmo、资产浏览和撤销重做。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：实现文件。

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#endif

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
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
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

std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return text;
}

std::string ComparablePath(const std::filesystem::path &path)
{
    if (path.empty())
        return {};
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        error.clear();
        normalized = std::filesystem::absolute(path, error).lexically_normal();
        if (error)
            normalized = path.lexically_normal();
    }
    std::string value = normalized.generic_string();
#ifdef _WIN32
    value = Lowercase(std::move(value));
#endif
    return value;
}

bool SamePath(const std::filesystem::path &left, const std::filesystem::path &right)
{
    return !left.empty() && !right.empty() && ComparablePath(left) == ComparablePath(right);
}

std::string ShortAssetName(const std::filesystem::path &path, const std::size_t maximum = 18)
{
    std::string name = path.filename().string();
    if (name.size() <= maximum)
        return name;
    return name.substr(0, maximum > 3 ? maximum - 3 : maximum) + "...";
}

#ifdef TINYENGINE_HAS_ASSIMP
std::string ValidateModelSource(const std::filesystem::path &source)
{
    if (source.empty())
        return "Select a source model file.";
    if (!std::filesystem::is_regular_file(source))
        return "The selected model file does not exist.";
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return "The selected model file cannot be read.";
    if (!AssimpImporter::CanImport(source))
        return "The selected file extension is not supported by Assimp.";
    return {};
}

const char *ModelSourceErrorText(const std::string &error, const bool simplifiedChinese)
{
    if (!simplifiedChinese)
        return error.c_str();
    if (error == "Select a source model file.")
        return "请选择源模型文件。";
    if (error == "The selected model file does not exist.")
        return "选择的模型文件不存在。";
    if (error == "The selected model file cannot be read.")
        return "选择的模型文件无法读取。";
    if (error == "The selected file extension is not supported by Assimp.")
        return "Assimp 不支持所选文件的扩展名。";
    return error.c_str();
}

#ifdef _WIN32
struct NativeFileDialogResult
{
    std::optional<std::filesystem::path> path;
    std::string error;
};

NativeFileDialogResult OpenModelFileDialog(const HWND owner, const std::filesystem::path &initialDirectory, const bool simplifiedChinese)
{
    NativeFileDialogResult result;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    {
        result.error = "Windows COM initialization failed.";
        return result;
    }

    IFileOpenDialog *dialog = nullptr;
    HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(status) || !dialog)
    {
        if (uninitialize)
            CoUninitialize();
        result.error = "Windows could not create the model file picker.";
        return result;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(simplifiedChinese ? L"选择要导入的模型" : L"Select a model to import");

    std::vector<std::string> supported = AssimpImporter::GetSupportedExtensions();
    for (auto &extension : supported)
    {
        extension = Lowercase(extension);
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
    }
    std::sort(supported.begin(), supported.end());
    supported.erase(std::unique(supported.begin(), supported.end()), supported.end());

    const auto buildPattern = [&supported](const std::vector<std::string> &extensions) {
        std::wstring pattern;
        for (const auto &requested : extensions)
            if (std::find(supported.begin(), supported.end(), requested) != supported.end())
            {
                if (!pattern.empty())
                    pattern += L';';
                const std::wstring wide(requested.begin(), requested.end());
                pattern += L'*' + wide;
            }
        return pattern;
    };
    const std::wstring allSupported = buildPattern(supported);
    std::vector<std::wstring> names;
    std::vector<std::wstring> patterns;
    const auto addFilter = [&names, &patterns](std::wstring name, std::wstring pattern) {
        if (!pattern.empty())
        {
            names.push_back(std::move(name));
            patterns.push_back(std::move(pattern));
        }
    };
    addFilter(simplifiedChinese ? L"所有支持的模型" : L"All supported models", allSupported);
    addFilter(L"FBX", buildPattern({".fbx"}));
    addFilter(L"glTF 2.0", buildPattern({".gltf", ".glb"}));
    addFilter(L"Wavefront OBJ", buildPattern({".obj"}));
    addFilter(simplifiedChinese ? L"所有文件" : L"All files", L"*.*");
    std::vector<COMDLG_FILTERSPEC> filters;
    filters.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
        filters.push_back({names[index].c_str(), patterns[index].c_str()});
    if (!filters.empty())
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());

    if (!initialDirectory.empty() && std::filesystem::is_directory(initialDirectory))
    {
        IShellItem *folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder)
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    status = dialog->Show(owner);
    if (SUCCEEDED(status))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item)
        {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)) && selected)
            {
                result.path = std::filesystem::path(selected).lexically_normal();
                CoTaskMemFree(selected);
            }
            item->Release();
        }
        if (!result.path)
            result.error = "Windows returned no model file from the picker.";
    }
    else if (status != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        result.error = "The Windows model file picker failed.";

    dialog->Release();
    if (uninitialize)
        CoUninitialize();
    return result;
}
#endif
#endif

bool LoadChineseEditorFont(ImGuiIO &io)
{
    static constexpr const char *fontCandidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
#endif
    };
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    for (const char *path : fontCandidates)
        if (std::filesystem::is_regular_file(path) &&
            io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, io.Fonts->GetGlyphRangesChineseFull()))
            return true;
    io.Fonts->AddFontDefault();
    return false;
}

std::string TranslateUi(const std::string_view text, const bool simplifiedChinese)
{
    if (!simplifiedChinese)
        return std::string(text);
    static const std::unordered_map<std::string_view, std::string_view> translations = {
        {"Name", "名称"},
        {"Active", "启用"},
        {"Transform", "变换"},
        {"Position", "位置"},
        {"Rotation", "旋转"},
        {"Scale", "缩放"},
        {"Tag", "标签"},
        {"Layer", "层"},
        {"Velocity", "速度"},
        {"Linear", "线速度"},
        {"Mesh Renderer", "网格渲染器"},
        {"Mesh", "网格"},
        {"Material", "材质"},
        {"Level Of Detail", "细节层级"},
        {"Level 1 Mesh", "一级网格"},
        {"Level 2 Mesh", "二级网格"},
        {"Level 1 Distance", "一级距离"},
        {"Level 2 Distance", "二级距离"},
        {"Camera", "相机"},
        {"FOV", "视野角"},
        {"Near Plane", "近裁剪面"},
        {"Far Plane", "远裁剪面"},
        {"Primary", "主相机"},
        {"Light", "灯光"},
        {"Type", "类型"},
        {"Directional", "平行光"},
        {"Point", "点光源"},
        {"Spot", "聚光灯"},
        {"Color", "颜色"},
        {"Intensity", "强度"},
        {"Direction", "方向"},
        {"Range", "范围"},
        {"Inner Cone", "内锥角"},
        {"Outer Cone", "外锥角"},
        {"Script", "脚本"},
        {"Class", "类"},
        {"Pawn", "角色控制体"},
        {"Player Controlled", "玩家控制"},
        {"Controller Index", "控制器编号"},
        {"Character Controller", "角色控制器"},
        {"Movement Speed", "移动速度"},
        {"Sprint Speed", "冲刺速度"},
        {"Jump Speed", "跳跃速度"},
        {"Gravity", "重力"},
        {"Air Control", "空中控制"},
        {"Grounded", "已着地"},
        {"Rigid Body", "刚体"},
        {"Static", "静态"},
        {"Dynamic", "动态"},
        {"Kinematic", "运动学"},
        {"Mass", "质量"},
        {"Use Gravity", "使用重力"},
        {"Linear Damping", "线性阻尼"},
        {"Restitution", "弹性"},
        {"Continuous", "连续碰撞"},
        {"Angular Velocity", "角速度"},
        {"Angular Damping", "角阻尼"},
        {"Friction", "摩擦系数"},
        {"Inertia Tensor", "惯性张量"},
        {"Lock Rotation X", "锁定 X 旋转"},
        {"Lock Rotation Y", "锁定 Y 旋转"},
        {"Lock Rotation Z", "锁定 Z 旋转"},
        {"Collider", "碰撞体"},
        {"Shape", "形状"},
        {"Box", "盒体"},
        {"Sphere", "球体"},
        {"Capsule", "胶囊体"},
        {"Convex Hull", "凸包"},
        {"Triangle Mesh", "三角网格"},
        {"Half Extents", "半尺寸"},
        {"Radius", "半径"},
        {"Height", "高度"},
        {"Trigger", "触发器"},
        {"Mask", "遮罩"},
        {"Joint", "约束"},
        {"Fixed", "固定"},
        {"Distance", "距离"},
        {"Hinge", "铰链"},
        {"Spring", "弹簧"},
        {"Connected Entity", "连接实体"},
        {"Rest Length", "静止长度"},
        {"Stiffness", "刚度"},
        {"Fixed Offset", "固定偏移"},
        {"Collide Connected", "连接体可碰撞"},
        {"Axis", "轴向"},
        {"Damping", "阻尼"},
        {"Minimum Limit", "最小限制"},
        {"Maximum Limit", "最大限制"},
        {"Break Force", "断裂力"},
        {"Animation", "动画"},
        {"Clip", "动画片段"},
        {"Skeleton", "骨架"},
        {"Time", "时间"},
        {"Speed", "速度"},
        {"Loop", "循环"},
        {"Playing", "播放中"},
        {"Next Clip", "下一片段"},
        {"Blend Duration", "混合时长"},
        {"Root Motion", "根运动"},
        {"Animation Graph", "动画图"},
        {"Graph Parameter", "图参数"},
        {"Graph Value", "图参数值"},
        {"Audio Source", "音频源"},
        {"Audio", "音频"},
        {"Volume", "音量"},
        {"Play On Start", "启动时播放"},
        {"Spatial", "空间音频"},
        {"Minimum Distance", "最小距离"},
        {"Maximum Distance", "最大距离"},
        {"Bus", "总线"},
        {"Texture", "纹理"},
        {"Shader", "着色器"},
        {"Scene", "场景"},
        {"Prefab", "预制体"},
        {"BlendTree", "混合树"},
        {"AnimationGraph", "动画图"},
        {"Environment", "环境"},
        {"MaterialGraph", "材质图"},
        {"Input", "输入映射"},
        {"Font", "字体"},
        {"Binary", "二进制"},
        {"Unknown", "未知"},
        {"Action", "动作"},
        {"Sequence", "序列"},
        {"Root", "根节点"},
        {"Constant", "常量"},
        {"Multiply", "乘法"},
        {"PBR Output", "PBR 输出"},
    };
    const auto found = translations.find(text);
    return found == translations.end() ? std::string(text) : std::string(found->second);
}
} // namespace

const char *EditorApp::Localize(const char *english, const char *simplifiedChinese) const
{
    return m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese ? simplifiedChinese : english;
}

std::string EditorApp::LocalizeReflected(const std::string_view text) const
{
    return TranslateUi(text, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
}

std::filesystem::path EditorApp::GetEditorSettingsPath()
{
    return m_engine.GetProjectSettings().Resolve("Saved/EditorSettings.ini");
}

void EditorApp::LoadEditorSettings()
{
    std::ifstream input(GetEditorSettingsPath());
    std::string line;
    while (std::getline(input, line))
    {
        if (line == "language=en")
            m_interfaceLanguage = InterfaceLanguage::English;
        else if (line == "language=zh-CN")
            m_interfaceLanguage = InterfaceLanguage::SimplifiedChinese;
        else if (line == "assetView=list")
            m_assetViewMode = AssetViewMode::List;
        else if (line == "assetView=grid")
            m_assetViewMode = AssetViewMode::Grid;
        else if (line.rfind("assetThumbnailSize=", 0) == 0)
        {
            try
            {
                m_assetThumbnailSize = std::clamp(std::stof(line.substr(19)), 64.0f, 160.0f);
            }
            catch (...)
            {
            }
        }
#ifdef TINYENGINE_HAS_ASSIMP
        else if (line.rfind("lastImportDirectory=", 0) == 0)
            m_lastImportDirectory = std::filesystem::u8path(line.substr(20));
#endif
    }
}

bool EditorApp::SaveEditorSettings()
{
    const std::filesystem::path path = GetEditorSettingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;
    output << "language=" << (m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese ? "zh-CN" : "en") << '\n';
    output << "assetView=" << (m_assetViewMode == AssetViewMode::Grid ? "grid" : "list") << '\n';
    output << "assetThumbnailSize=" << m_assetThumbnailSize << '\n';
#ifdef TINYENGINE_HAS_ASSIMP
    output << "lastImportDirectory=" << m_lastImportDirectory.generic_u8string() << '\n';
#endif
    return output.good();
}

// 编辑器复用 Engine 的 World 与服务，但以编辑态启动，并把 ImGui 回调挂到场景渲染之后。
bool EditorApp::Initialize(const std::filesystem::path &projectFile)
{
    if (!m_engine.Initialize(projectFile, false))
        return false;
    LoadEditorSettings();
    m_assetFolder = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory).lexically_normal();
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
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), Localize("TinyEngine Editor", "TinyEngine 编辑器"));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const bool chineseFontLoaded = LoadChineseEditorFont(io);
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
    m_messages.emplace_back(
        Localize("Editor initialized. Edit and play states are isolated.", "编辑器初始化完成。编辑状态与运行模式相互隔离。"));
    if (!chineseFontLoaded)
        m_messages.emplace_back(Localize("Warning: no CJK font was found. Chinese text may not display correctly.",
                                         "警告：未找到中文字体，中文界面可能无法正确显示。"));
    m_sceneDocuments.push_back({m_engine.GetSceneManager().GetCurrentScene(), m_engine.GetSceneManager().CreateSnapshot(), false});
#ifdef TINYENGINE_HAS_ASSIMP
    m_importQueue.Initialize(m_engine.GetContext().jobSystem);
#endif
    if (std::filesystem::is_regular_file(GetAutosavePath()))
        m_messages.emplace_back(
            Localize("An autosave is available from File > Restore Autosave.", "检测到自动恢复文件，可通过“文件 → 恢复自动保存”载入。"));
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
    DrawSceneSaveDialog();
    DrawSceneCloseDialog();
    // 浮动设置窗口最后提交，保证它位于固定布局面板之上。
    DrawSettings();
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z))
            Undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y))
            Redo();
        if (ImGui::IsKeyPressed(ImGuiKey_S))
        {
            if (io.KeyShift)
                RequestSaveCurrentSceneAs();
            else
                SaveCurrentScene();
        }
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
    if (ImGui::BeginMenu(Localize("File", "文件")))
    {
        if (ImGui::MenuItem(Localize("Save Scene", "保存场景"), "Ctrl+S"))
            SaveCurrentScene();
        if (ImGui::MenuItem(Localize("Save Scene As...", "场景另存为..."), "Ctrl+Shift+S"))
            RequestSaveCurrentSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem(Localize("New Scene", "新建场景")))
            NewSceneDocument();
        const auto autosavePath = GetAutosavePath();
        ImGui::BeginDisabled(!std::filesystem::is_regular_file(autosavePath) || m_engine.IsPlaying());
        if (ImGui::MenuItem(Localize("Restore Autosave", "恢复自动保存")))
            RestoreAutosave();
        ImGui::EndDisabled();
        const bool canPackage = !m_engine.IsPlaying() && !m_engine.GetProjectSettings().projectFile.empty();
        ImGui::BeginDisabled(!canPackage);
        if (ImGui::MenuItem(Localize("Package Project", "打包项目")))
        {
            if (SaveCurrentScene())
                PackageProject();
            else if (m_sceneSaveDocument != InvalidSceneDocument)
            {
                m_packageAfterSceneSave = true;
                m_messages.emplace_back(Localize("Packaging will continue after Save As succeeds.", "另存为成功后将继续打包。"));
            }
            else
                m_messages.emplace_back(Localize("Packaging stopped because the scene could not be saved.",
                                                 "场景保存失败，已停止打包。"));
        }
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Localize("Edit", "编辑")))
    {
        if (ImGui::MenuItem(Localize("Undo", "撤销"), "Ctrl+Z", false, !m_undo.empty()))
            Undo();
        if (ImGui::MenuItem(Localize("Redo", "重做"), "Ctrl+Y", false, !m_redo.empty()))
            Redo();
        ImGui::Separator();
        if (ImGui::MenuItem(Localize("Settings...", "设置...")))
            m_showSettings = true;
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (!m_engine.IsPlaying())
    {
        if (ImGui::Button(Localize("Play", "运行")))
        {
            m_engine.EnterPlayMode();
            m_messages.emplace_back(Localize("Entered play mode", "已进入运行模式"));
        }
    }
    else
    {
        if (ImGui::Button(Localize("Stop", "停止")))
        {
            m_engine.ExitPlayMode();
            m_selected = InvalidEntity;
            m_messages.emplace_back(Localize("Play mode stopped; edit scene restored", "运行模式已停止，编辑场景已恢复"));
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_engine.IsPlaying() ? Localize("Playing", "运行中") : Localize("Editing", "编辑中"));
    ImGui::EndMainMenuBar();
}

void EditorApp::PackageProject()
{
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
        m_messages.emplace_back(std::string(Localize("Project packaged: ", "项目打包完成：")) + result.outputDirectory.generic_string());
    else
        for (const auto &error : result.errors)
            m_messages.emplace_back(std::string(Localize("Package error: ", "打包错误：")) + error);
}

void EditorApp::DrawSettings()
{
    if (!m_showSettings)
        return;
    ImGui::SetNextWindowSize({520.0f, 0.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Localize("Settings###EditorSettingsWindow", "设置###EditorSettingsWindow"), &m_showSettings,
                      ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    int language = m_interfaceLanguage == InterfaceLanguage::English ? 0 : 1;
    if (ImGui::Combo(Localize("Interface Language", "界面语言"), &language, "English\0简体中文\0"))
    {
        m_interfaceLanguage = language == 0 ? InterfaceLanguage::English : InterfaceLanguage::SimplifiedChinese;
        m_titleInitialized = false;
        const bool saved = SaveEditorSettings();
        m_messages.emplace_back(saved ? Localize("Interface language saved.", "界面语言已保存。")
                                      : Localize("Failed to save interface language.", "界面语言保存失败。"));
    }
    ImGui::TextDisabled(
        "%s", Localize("The setting is applied immediately and restored at the next launch.", "设置立即生效，并会在下次启动时自动恢复。"));
    if (ImGui::Button(Localize("Close", "关闭")))
        m_showSettings = false;
    ImGui::End();
}

void EditorApp::DrawHierarchy()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y;
    const float upperHeight = viewport->WorkSize.y * 0.67f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({260.0f, upperHeight}, ImGuiCond_Always);
    ImGui::Begin(Localize("Hierarchy", "场景层级"));
    World &world = *m_engine.GetContext().world;
    if (!m_engine.IsPlaying() && ImGui::Button(Localize("+ Entity", "+ 实体")))
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
                                            name ? name->name.c_str() : Localize("Entity", "实体"));
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
    ImGui::Begin(Localize("Inspector", "属性检查器"));
    if (m_selection.size() > 1)
        ImGui::TextDisabled(Localize("%zu entities selected; editing the active entity", "已选择 %zu 个实体；正在编辑主实体"),
                            m_selection.size());
    World &world = *m_engine.GetContext().world;
    if (!world.IsAlive(m_selected))
    {
        const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                   : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
        if (asset && asset->type == "Material")
        {
            if (auto material = m_engine.GetContext().resourceManager->Load<MaterialResource>(asset->path))
            {
                ImGui::SeparatorText(Localize("Material Asset", "材质资产"));
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
                changed = ImGui::ColorEdit4(Localize("Base Color", "基础颜色"), &color.x) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("metallic"));
                changed = ImGui::SliderFloat(Localize("Metallic", "金属度"), &metallic, 0.0f, 1.0f) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("roughness"));
                changed = ImGui::SliderFloat(Localize("Roughness", "粗糙度"), &roughness, 0.04f, 1.0f) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("emissive"));
                changed = ImGui::ColorEdit3(Localize("Emissive", "自发光"), &emissive.x) || changed;
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!overrideControl("blend"));
                changed = ImGui::Checkbox(Localize("Transparent", "透明"), &transparent) || changed;
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
                editPath(Localize("Parent Material", "父材质"), "parent", parent);
                editPath(Localize("Shader", "着色器"), "shader", shader);
                editPath(Localize("Base Color Texture", "基础色贴图"), "texture", albedo);
                editPath(Localize("Normal Texture", "法线贴图"), "normalTexture", normal);
                editPath(Localize("Metallic Texture", "金属度贴图"), "metallicTexture", metallicMap);
                editPath(Localize("Roughness Texture", "粗糙度贴图"), "roughnessTexture", roughnessMap);
                editPath(Localize("Ambient Occlusion Texture", "环境光遮蔽贴图"), "aoTexture", ao);
                editPath(Localize("Emissive Texture", "自发光贴图"), "emissiveTexture", emissiveMap);
                editPath(Localize("Packed ORM/RMA Texture", "打包 ORM/RMA 贴图"), "packedTexture", packed);
                if (overrideControl("packedLayout"))
                {
                    int layout = packedLayout == "RMA" ? 1 : 0;
                    if (ImGui::Combo(Localize("Packed Channel Layout", "打包通道布局"), &layout, "ORM\0RMA\0"))
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
                        m_messages.emplace_back(std::string(Localize("Material saved: ", "材质已保存：")) + asset->path);
                    else
                        m_messages.emplace_back(std::string(Localize("Failed to save material: ", "材质保存失败：")) + asset->path);
                }
            }
        }
        else
            ImGui::TextDisabled("%s", Localize("Select an entity or material asset", "请选择实体或材质资产"));
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
        const std::string componentLabel = LocalizeReflected(component.name);
        ImGui::SeparatorText(componentLabel.c_str());
        for (const auto &property : component.properties)
        {
            ImGui::PushID(property.name.c_str());
            const std::string propertyLabel = LocalizeReflected(property.name);
            void *value = property.access ? property.access(world, m_selected) : nullptr;
            bool changed = false;
            switch (property.type)
            {
            case ReflectedPropertyType::Bool: {
                bool edited = value ? *static_cast<bool *>(value) : false;
                changed = value && ImGui::Checkbox(propertyLabel.c_str(), &edited);
                if (changed)
                {
                    RecordUndo();
                    *static_cast<bool *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::UInt: {
                std::uint32_t edited = value ? *static_cast<std::uint32_t *>(value) : 0;
                changed = value && ImGui::InputScalar(propertyLabel.c_str(), ImGuiDataType_U32, &edited);
                if (changed)
                {
                    RecordUndo();
                    *static_cast<std::uint32_t *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::Entity: {
                Entity edited = value ? *static_cast<Entity *>(value) : InvalidEntity;
                changed = value && ImGui::InputScalar(propertyLabel.c_str(), ImGuiDataType_U64, &edited);
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
                changed = value && ImGui::DragFloat(propertyLabel.c_str(), &edited, property.speed, minimum, maximum);
                if (ImGui::IsItemActivated())
                    RecordUndo();
                if (changed)
                    *static_cast<float *>(value) = edited;
                break;
            }
            case ReflectedPropertyType::Vec3: {
                Vec3 edited = value ? *static_cast<Vec3 *>(value) : Vec3{};
                if (property.color)
                    changed = value && ImGui::ColorEdit3(propertyLabel.c_str(), &edited.x);
                else
                {
                    const float minimum = property.hasRange ? property.minimum : 0.0f;
                    const float maximum = property.hasRange ? property.maximum : 0.0f;
                    changed = value && ImGui::DragFloat3(propertyLabel.c_str(), &edited.x, property.speed, minimum, maximum);
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
                if (text && ImGui::InputText(propertyLabel.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
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
                            m_messages.emplace_back(std::string(Localize("Inspector rejected asset ", "属性检查器拒绝资产 ")) + path +
                                                    Localize("; expected type: ", "；期望类型：") + LocalizeReflected(property.assetType));
                    }
                    ImGui::EndDragDropTarget();
                }
                break;
            }
            case ReflectedPropertyType::Enum: {
                int edited = property.getEnum ? property.getEnum(world, m_selected) : 0;
                const std::string preview = edited >= 0 && static_cast<std::size_t>(edited) < property.enumNames.size()
                                                ? LocalizeReflected(property.enumNames[edited])
                                                : Localize("Unknown", "未知");
                if (ImGui::BeginCombo(propertyLabel.c_str(), preview.c_str()))
                {
                    for (std::size_t index = 0; index < property.enumNames.size(); ++index)
                    {
                        const std::string enumLabel = LocalizeReflected(property.enumNames[index]);
                        if (ImGui::Selectable(enumLabel.c_str(), edited == static_cast<int>(index)))
                        {
                            RecordUndo();
                            property.setEnum(world, m_selected, static_cast<int>(index));
                            changed = true;
                        }
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
            if (ImGui::SmallButton(Localize("Remove Component", "移除组件")))
            {
                RecordUndo();
                component.remove(world, m_selected);
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    if (ImGui::Button(Localize("Add Component", "添加组件")))
        ImGui::OpenPopup("AddComponentPopup");
    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        for (const auto &component : registry.GetComponents())
        {
            const std::string componentLabel = LocalizeReflected(component.name);
            if (component.removable && !component.has(world, m_selected) && ImGui::MenuItem(componentLabel.c_str()))
            {
                RecordUndo();
                component.add(world, m_selected);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

// Scene View 同时处理相机、点选、框选和 Gizmo；输入优先级保证拖动 Gizmo 时不会误触场景选择。
void EditorApp::DrawSceneView()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float left = viewport->WorkPos.x + 260.0f, top = viewport->WorkPos.y;
    const float width = std::max(160.0f, viewport->WorkSize.x - 610.0f), height = viewport->WorkSize.y * 0.67f;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::Begin(Localize("Scene View", "场景视图"), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    DrawSceneTabs();
    const char *pivotNames[] = {Localize("Center", "中心"), Localize("Active", "活动项"), Localize("Individual", "各自")};
    float *snapValue = m_gizmoMode == GizmoMode::Translate ? &m_translateSnap
                       : m_gizmoMode == GizmoMode::Rotate  ? &m_rotateSnap
                                                           : &m_scaleSnap;
    float exposure = m_device->GetExposure();
    bool fxaa = m_device->IsFxaaEnabled();
    bool bloom = m_device->IsBloomEnabled(), gtao = m_device->IsGtaoEnabled(), taa = m_device->IsTaaEnabled();
    const auto reloadShaders = [this]() {
        if (m_device->ReloadShaders())
            m_messages.emplace_back(Localize("Shaders reloaded", "着色器已重载"));
        else
            m_messages.emplace_back(
                std::string(Localize("Shader reload failed; kept previous version: ", "着色器重载失败，已保留上一版本：")) +
                m_device->GetLastShaderError());
    };
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    if (toolbarWidth < 520.0f)
    {
        // 窄窗口只保留三个模式键，其余控制集中到弹出菜单；所有功能始终可访问。
        if (ImGui::Button("W"))
            m_gizmoMode = GizmoMode::Translate;
        ImGui::SameLine();
        if (ImGui::Button("E"))
            m_gizmoMode = GizmoMode::Rotate;
        ImGui::SameLine();
        if (ImGui::Button("R"))
            m_gizmoMode = GizmoMode::Scale;
        ImGui::SameLine();
        if (ImGui::Button(Localize("View Options...", "视图选项...")))
            ImGui::OpenPopup("SceneViewOptions");
        if (ImGui::BeginPopup("SceneViewOptions"))
        {
            ImGui::Checkbox(Localize("Snap", "吸附"), &m_gizmoSnap);
            if (ImGui::MenuItem(m_gizmoSpace == GizmoSpace::World ? Localize("Space: World", "空间：世界")
                                                                   : Localize("Space: Local", "空间：局部")))
                m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
            int pivot = static_cast<int>(m_pivotMode);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo(Localize("Pivot", "轴心"), &pivot, pivotNames, 3))
                m_pivotMode = static_cast<PivotMode>(pivot);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat(Localize("Snap Value", "吸附值"), snapValue, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragFloat(Localize("Exposure", "曝光"), &exposure, 0.02f, 0.05f, 16.0f, "%.2f"))
                m_device->SetExposure(exposure);
            if (ImGui::Checkbox("FXAA", &fxaa))
                m_device->SetFxaaEnabled(fxaa);
            if (ImGui::Checkbox(Localize("Bloom", "泛光"), &bloom))
                m_device->SetBloomEnabled(bloom);
            if (ImGui::Checkbox("GTAO", &gtao))
                m_device->SetGtaoEnabled(gtao);
            if (ImGui::Checkbox("TAA", &taa))
                m_device->SetTaaEnabled(taa);
            if (ImGui::Button(Localize("Reload Shaders", "重载着色器")))
                reloadShaders();
            ImGui::EndPopup();
        }
    }
    else
    {
        if (ImGui::Button(Localize("W Translate", "W 移动")))
            m_gizmoMode = GizmoMode::Translate;
        ImGui::SameLine();
        if (ImGui::Button(Localize("E Rotate", "E 旋转")))
            m_gizmoMode = GizmoMode::Rotate;
        ImGui::SameLine();
        if (ImGui::Button(Localize("R Scale", "R 缩放")))
            m_gizmoMode = GizmoMode::Scale;
        ImGui::SameLine();
        ImGui::Checkbox(Localize("Snap", "吸附"), &m_gizmoSnap);
        ImGui::SameLine();
        const char *spaceLabel = m_gizmoSpace == GizmoSpace::World ? Localize("World", "世界") : Localize("Local", "局部");
        if (ImGui::Button(spaceLabel))
            m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
        ImGui::SameLine();
        int pivot = static_cast<int>(m_pivotMode);
        ImGui::SetNextItemWidth(82.0f);
        if (ImGui::Combo("##Pivot", &pivot, pivotNames, 3))
            m_pivotMode = static_cast<PivotMode>(pivot);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("##SnapValue", snapValue, 0.05f, 0.001f, 100.0f, "%.2f");
        // 后处理单独一行，不再依赖超宽的单行工具栏。
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::DragFloat(Localize("Exposure", "曝光"), &exposure, 0.02f, 0.05f, 16.0f, "%.2f"))
            m_device->SetExposure(exposure);
        ImGui::SameLine();
        if (ImGui::Checkbox("FXAA", &fxaa))
            m_device->SetFxaaEnabled(fxaa);
        ImGui::SameLine();
        if (ImGui::Checkbox(Localize("Bloom", "泛光"), &bloom))
            m_device->SetBloomEnabled(bloom);
        ImGui::SameLine();
        if (ImGui::Checkbox("GTAO", &gtao))
            m_device->SetGtaoEnabled(gtao);
        ImGui::SameLine();
        if (ImGui::Checkbox("TAA", &taa))
            m_device->SetTaaEnabled(taa);
        ImGui::SameLine();
        if (ImGui::SmallButton(Localize("Reload Shaders", "重载着色器")))
            reloadShaders();
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
                {
                    for (const Entity entity : marquee)
                        if (std::find(m_selection.begin(), m_selection.end(), entity) == m_selection.end())
                            m_selection.push_back(entity);
                }
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
                if (asset->type == "Material" && world.IsAlive(m_selected))
                {
                    if (auto *mesh = world.GetComponent<MeshComponent>(m_selected))
                    {
                        RecordUndo();
                        mesh->materialPath = path;
                        m_messages.emplace_back(std::string(Localize("Applied material: ", "已应用材质：")) + path);
                    }
                }
                else if (!PlaceAssetInScene(*asset))
                    m_messages.emplace_back(
                        std::string(Localize("This asset cannot be placed in the scene: ", "此资产不能直接放入场景：")) + path);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

// 从归一化视口坐标反投影射线，并选取最近包围盒交点；不可见或无几何边界的实体不会被命中。
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

// 多选时先求枢轴，再把同一增量应用到各实体；开始拖动时仅记录一次 Undo 快照。
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

// 依据资产类型选择放置策略：Prefab 重建完整层级，Mesh 则创建或替换单个 MeshRenderer。
bool EditorApp::PlaceAssetInScene(const AssetInfo &asset)
{
    if (m_engine.IsPlaying())
        return false;
    World &world = *m_engine.GetContext().world;
    if (asset.type == "Prefab")
    {
        RecordUndo();
        const auto roots = Prefab::Instantiate(world, asset.path);
        if (roots.empty())
        {
            if (!m_undo.empty())
                m_undo.pop_back();
            return false;
        }
        m_selection = roots;
        m_selected = roots.front();
    }
    else if (asset.type == "Mesh")
    {
        RecordUndo();
        m_selected = world.CreateEntity(std::filesystem::path(asset.path).stem().string());
        world.AddComponent(m_selected, MeshComponent{asset.path, "assets/default.mat"});
        m_selection = {m_selected};
    }
    else
        return false;
    m_messages.emplace_back(std::string(Localize("Placed in scene: ", "已放入场景：")) + asset.path);
    return true;
}

std::filesystem::path EditorApp::GetThumbnailPath(const AssetInfo &asset) const
{
    const int size = static_cast<int>(std::clamp(m_assetThumbnailSize, 64.0f, 160.0f));
    return std::filesystem::path{"Saved/Thumbnails"} / (asset.id + "_" + std::to_string(size) + ".ppm");
}

bool EditorApp::IsThumbnailReady(const AssetInfo &asset, const std::filesystem::path &path) const
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return false;
    const auto cached = std::filesystem::last_write_time(path, error);
    if (error || cached < asset.modified)
        return false;
    for (const auto &dependency : asset.dependencies)
    {
        const auto modified = std::filesystem::last_write_time(dependency, error);
        if (!error && modified > cached)
            return false;
        error.clear();
    }
    return true;
}

// 缩略图生成提交到 JobSystem；m_thumbnailJobs 去重，UI 线程只轮询完成状态而不阻塞。
void EditorApp::RequestThumbnail(const AssetInfo &asset, const bool force)
{
    const auto path = GetThumbnailPath(asset);
    if (auto existing = m_thumbnailJobs.find(asset.id); existing != m_thumbnailJobs.end())
    {
        if (!existing->second->IsFinished())
            return;
        m_thumbnailJobs.erase(existing);
    }
    if (!force && IsThumbnailReady(asset, path))
        return;
    if (force)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    const int size = static_cast<int>(std::clamp(m_assetThumbnailSize, 64.0f, 160.0f));
    auto job = m_engine.GetContext().jobSystem->CreateJob(
        [asset, path, size]() {
            std::string error;
            AssetMaintenance::GenerateThumbnail(asset, path, size, &error);
        },
        JobPriority::Low);
    m_thumbnailJobs[asset.id] = job;
    m_engine.GetContext().jobSystem->Submit(job);
}

void EditorApp::DrawAssets()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
    const float halfWidth = viewport->WorkSize.x * 0.58f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
    ImGui::Begin(Localize("Assets", "资产"));
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
        const auto ensureTemplateClip = [this, &createUniquePath]() -> std::optional<std::string> {
            auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
            std::sort(assets.begin(), assets.end(), [](const AssetInfo &left, const AssetInfo &right) { return left.path < right.path; });
            std::filesystem::path clipPath;
            for (const auto &asset : assets)
                if (asset.extension == ".skanim" && PlatformFile::Exists(asset.path))
                {
                    clipPath = asset.path;
                    break;
                }
            if (clipPath.empty())
            {
                clipPath = createUniquePath("NewAnimationClip", ".skanim");
                SkeletalClip clip;
                clip.name = clipPath.stem().string();
                clip.duration = 1.0f;
                clip.keyframes = {{0.0f, {BonePose{}}}, {1.0f, {BonePose{}}}};
                if (!SaveSkeletalClipAsset(clip, clipPath))
                    return std::nullopt;
            }
            std::error_code error;
            const auto relative = std::filesystem::relative(clipPath, m_engine.GetProjectSettings().projectFile.parent_path(), error);
            return error || relative.empty() ? clipPath.lexically_normal().generic_string() : relative.lexically_normal().generic_string();
        };
        if (ImGui::Button(Localize("+ Blend Tree", "+ 混合树")))
        {
            const auto clip = ensureTemplateClip();
            if (!clip)
                m_messages.emplace_back(Localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"));
            else
            {
                BlendTree1D tree;
                tree.samples = {{0.0f, *clip}};
                const auto path = createUniquePath("NewBlendTree", ".blendtree");
                if (SaveBlendTreeAsset(tree, path))
                {
                    auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
                    registry.Scan(path.parent_path());
                    registry.SetDependencies(path.generic_string(), {*clip});
                    m_selectedAsset = path.generic_string();
                    m_selectedAssets = {m_selectedAsset};
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("+ Animation Graph", "+ 动画图")))
        {
            const auto clip = ensureTemplateClip();
            if (!clip)
                m_messages.emplace_back(Localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"));
            else
            {
                AnimationGraph graph;
                graph.entryState = "Idle";
                graph.parameters = {{"Speed", 0.0f}};
                graph.states = {{"Idle", *clip}};
                const auto path = createUniquePath("NewAnimationGraph", ".animgraph");
                if (SaveAnimationGraphAsset(graph, path))
                {
                    auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
                    registry.Scan(path.parent_path());
                    registry.SetDependencies(path.generic_string(), {*clip});
                    m_selectedAsset = path.generic_string();
                    m_selectedAssets = {m_selectedAsset};
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("+ VFX", "+ 特效")))
        {
            const auto path = createUniquePath("NewEffect", ".vfx");
            if (SaveParticleEffectAsset({}, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
                m_selectedAssets = {m_selectedAsset};
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("+ Behavior Tree", "+ 行为树")))
        {
            BehaviorTreeGraph graph;
            graph.root = 1;
            graph.nodes = {{1, "Sequence", "Root", {20.0f, 20.0f}, {2}}, {2, "Action", "Action", {220.0f, 20.0f}, {}}};
            const auto path = createUniquePath("NewBehavior", ".btgraph");
            if (SaveBehaviorTreeGraph(graph, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
                m_selectedAssets = {m_selectedAsset};
            }
        }
        ImGui::Separator();
    }
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask != 0)
    {
        const auto task = m_importQueue.Get(m_activeImportTask);
        ImGui::ProgressBar(task.progress, ImVec2(220.0f, 0.0f),
                           task.status == AssetImportStatus::Queued    ? Localize("Queued", "等待中")
                           : task.status == AssetImportStatus::Running ? Localize("Importing", "正在导入")
                                                                       : Localize("Finishing", "正在完成"));
        if ((task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) &&
            ImGui::Button(Localize("Cancel Import", "取消导入")))
            m_importQueue.Cancel(m_activeImportTask);
        if (task.status == AssetImportStatus::Completed || task.status == AssetImportStatus::Failed ||
            task.status == AssetImportStatus::Cancelled)
        {
            if (task.status == AssetImportStatus::Completed)
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(
                    m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
                if (!task.result.prefabPath.empty())
                {
                    m_selectedAsset = task.result.prefabPath.lexically_normal().generic_string();
                    m_selectedAssets = {m_selectedAsset};
                    m_assetFolder = task.result.prefabPath.parent_path().lexically_normal();
                }
                m_messages.emplace_back(Localize(
                    "Model import completed. The Prefab is selected; double-click it, drag it to Scene View, or use Place in Scene.",
                    "模型导入完成，已选中 Prefab；可双击、拖到场景视图，或使用“放入场景”。"));
            }
            else if (task.status == AssetImportStatus::Cancelled)
                m_messages.emplace_back(Localize("Background model import cancelled", "后台模型导入已取消"));
            else
                m_messages.emplace_back(std::string(Localize("Background model import failed: ", "后台模型导入失败：")) +
                                        task.result.error);
            for (const auto &warning : task.result.warnings)
                m_messages.emplace_back(std::string(Localize("Import warning: ", "导入警告：")) + warning);
            m_importQueue.ClearFinished();
            m_activeImportTask = 0;
        }
        ImGui::Separator();
    }
    ImGui::BeginDisabled(m_activeImportTask != 0);
    if (ImGui::Button(Localize("Import Model...", "导入模型...")))
    {
#ifdef _WIN32
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        HWND owner = nullptr;
        if (SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()), &windowInfo) == SDL_TRUE)
            owner = windowInfo.info.win.window;
        const auto selected =
            OpenModelFileDialog(owner, m_lastImportDirectory, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
        if (selected.path)
        {
            m_importSource = *selected.path;
            const std::string path = selected.path->generic_u8string();
            std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
            std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
            m_lastImportDirectory = selected.path->parent_path();
            m_importValidationError = ValidateModelSource(*selected.path);
            SaveEditorSettings();
            ImGui::OpenPopup(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
        }
        else if (!selected.error.empty())
            m_messages.emplace_back(std::string(Localize("File picker failed: ", "文件选择器失败：")) + selected.error);
#else
        m_importSource = std::filesystem::u8path(m_importSourcePath.data());
        m_importValidationError = ValidateModelSource(m_importSource);
        ImGui::OpenPopup(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
#endif
    }
    ImGui::SameLine();
    const auto selectedInfo = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                      : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" || selectedInfo->importSettings.count("source") == 0 ||
                         m_activeImportTask != 0);
    if (ImGui::Button(Localize("Reimport", "重新导入")))
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
        m_messages.emplace_back(std::string(Localize("Model reimport queued: ", "模型重新导入已加入队列：")) + source.generic_string());
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(520.0f);
#ifdef _WIN32
        ImGui::InputText(Localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size(),
                         ImGuiInputTextFlags_ReadOnly);
#else
        if (ImGui::InputText(Localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size()))
        {
            m_importSource = std::filesystem::u8path(m_importSourcePath.data());
            m_importValidationError = ValidateModelSource(m_importSource);
        }
#endif
#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button(Localize("Browse...", "浏览...")))
        {
            SDL_SysWMinfo windowInfo{};
            SDL_VERSION(&windowInfo.version);
            HWND owner = nullptr;
            if (SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()), &windowInfo) == SDL_TRUE)
                owner = windowInfo.info.win.window;
            const auto selected =
                OpenModelFileDialog(owner, m_lastImportDirectory, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
            if (selected.path)
            {
                m_importSource = *selected.path;
                const std::string path = selected.path->generic_u8string();
                std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
                std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
                m_lastImportDirectory = selected.path->parent_path();
                m_importValidationError = ValidateModelSource(*selected.path);
                SaveEditorSettings();
            }
            else if (!selected.error.empty())
                m_importValidationError = selected.error;
        }
#else
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            Localize("Native file picker is currently available on Windows.", "原生文件选择器目前仅在 Windows 上提供。"));
#endif
        if (!m_importValidationError.empty())
            ImGui::TextColored({1.0f, 0.35f, 0.28f, 1.0f}, "%s",
                               ModelSourceErrorText(m_importValidationError, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese));
        else
            ImGui::TextColored({0.35f, 0.85f, 0.45f, 1.0f}, "%s", Localize("Ready to import", "文件有效，可以导入"));
        ImGui::DragFloat(Localize("Scale", "缩放"), &m_importScale, 0.01f, 0.0001f, 1000.0f);
        ImGui::Checkbox(Localize("Flip UVs", "翻转 UV"), &m_importFlipUvs);
        ImGui::Checkbox(Localize("Compress Animations", "压缩动画"), &m_importCompressAnimations);
        ImGui::BeginDisabled(!m_importCompressAnimations);
        ImGui::DragFloat(Localize("Position Error", "位置误差"), &m_importPositionTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::DragFloat(Localize("Rotation Error (Degrees)", "旋转误差（度）"), &m_importRotationTolerance, 0.01f, 0.0f, 45.0f, "%.3f");
        ImGui::DragFloat(Localize("Scale Error", "缩放误差"), &m_importScaleTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_importValidationError.empty() || m_importSourcePath[0] == '\0');
        if (ImGui::Button(Localize("Import", "导入")))
        {
            const std::filesystem::path source = m_importSource;
            const std::string folderName = source.stem().empty() ? "Model" : source.stem().string();
            const auto relativeOutput = m_engine.GetProjectSettings().assetDirectory / "Imported" / folderName;
            const auto output = m_engine.GetProjectSettings().Resolve(relativeOutput);
            ModelImportOptions options;
            options.scale = m_importScale;
            options.flipUvs = m_importFlipUvs;
            options.compressAnimations = m_importCompressAnimations;
            options.animationPositionTolerance = m_importPositionTolerance;
            options.animationRotationToleranceDegrees = m_importRotationTolerance;
            options.animationScaleTolerance = m_importScaleTolerance;
            options.resourcePrefix = relativeOutput;
            m_activeImportTask = m_importQueue.Submit(source, output, options);
            m_messages.emplace_back(std::string(Localize("Model import queued: ", "模型导入已加入队列：")) + source.generic_string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();
#endif
    auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
    std::sort(assets.begin(), assets.end(), [](const AssetInfo &a, const AssetInfo &b) { return a.path < b.path; });
    static std::string moveSource;
    static std::array<char, 1024> moveDestination{};
    const auto assetRoot = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory).lexically_normal();
    std::error_code pathError;
    const auto folderRelative = std::filesystem::relative(m_assetFolder, assetRoot, pathError);
    if (m_assetFolder.empty() || pathError || (!folderRelative.empty() && *folderRelative.begin() == ".."))
        m_assetFolder = assetRoot;

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##AssetSearch", Localize("Search assets...", "搜索资产..."), m_assetSearch.data(), m_assetSearch.size());
    ImGui::SameLine();
    std::set<std::string> availableTypes;
    for (const auto &asset : assets)
        availableTypes.insert(asset.type);
    ImGui::SetNextItemWidth(130.0f);
    const std::string currentType = m_assetTypeFilter.empty() ? Localize("All Types", "全部类型") : LocalizeReflected(m_assetTypeFilter);
    if (ImGui::BeginCombo("##AssetType", currentType.c_str()))
    {
        if (ImGui::Selectable(Localize("All Types", "全部类型"), m_assetTypeFilter.empty()))
            m_assetTypeFilter.clear();
        for (const auto &type : availableTypes)
        {
            const std::string label = LocalizeReflected(type);
            if (ImGui::Selectable(label.c_str(), m_assetTypeFilter == type))
                m_assetTypeFilter = type;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_assetViewMode == AssetViewMode::Grid ? Localize("List", "列表") : Localize("Grid", "网格")))
    {
        m_assetViewMode = m_assetViewMode == AssetViewMode::Grid ? AssetViewMode::List : AssetViewMode::Grid;
        SaveEditorSettings();
    }
    if (m_assetViewMode == AssetViewMode::Grid)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderFloat("##ThumbnailSize", &m_assetThumbnailSize, 64.0f, 160.0f, Localize("%.0f px", "%.0f 像素")))
            SaveEditorSettings();
    }

    if (ImGui::SmallButton(Localize("Assets", "资产")))
        m_assetFolder = assetRoot;
    std::filesystem::path breadcrumb = assetRoot;
    pathError.clear();
    const auto relativeFolder = std::filesystem::relative(m_assetFolder, assetRoot, pathError);
    if (!pathError)
        for (const auto &part : relativeFolder)
        {
            if (part == ".")
                continue;
            breadcrumb /= part;
            ImGui::SameLine();
            ImGui::TextUnformatted(">");
            ImGui::SameLine();
            const std::string partName = part.string();
            if (ImGui::SmallButton(partName.c_str()))
                m_assetFolder = breadcrumb;
        }

    const std::string search = Lowercase(m_assetSearch.data());
    const auto selectAsset = [this](const std::string &path) {
        if (ImGui::GetIO().KeyCtrl)
        {
            const auto selected = std::find(m_selectedAssets.begin(), m_selectedAssets.end(), path);
            if (selected == m_selectedAssets.end())
                m_selectedAssets.push_back(path);
            else
                m_selectedAssets.erase(selected);
            m_selectedAsset = m_selectedAssets.empty() ? std::string{} : m_selectedAssets.back();
        }
        else
        {
            m_selectedAsset = path;
            m_selectedAssets = {path};
        }
    };
    const auto regenerateSelectedThumbnails = [this, &assets](const AssetInfo &fallback) {
        const bool batch = m_selectedAssets.size() > 1 &&
                           std::find(m_selectedAssets.begin(), m_selectedAssets.end(), fallback.path) != m_selectedAssets.end();
        if (!batch)
        {
            RequestThumbnail(fallback, true);
            return;
        }
        for (const auto &path : m_selectedAssets)
            if (const auto found =
                    std::find_if(assets.begin(), assets.end(), [&path](const AssetInfo &asset) { return asset.path == path; });
                found != assets.end())
                RequestThumbnail(*found, true);
    };
    std::set<std::filesystem::path> childFolders;
    std::vector<const AssetInfo *> visibleAssets;
    for (const auto &asset : assets)
    {
        const std::filesystem::path path = std::filesystem::path(asset.path).lexically_normal();
        const std::string searchable = Lowercase(path.filename().string() + " " + asset.type + " " + asset.path);
        if (!search.empty() && searchable.find(search) == std::string::npos)
            continue;
        if (!m_assetTypeFilter.empty() && asset.type != m_assetTypeFilter)
            continue;
        if (search.empty())
        {
            pathError.clear();
            const auto relative = std::filesystem::relative(path, m_assetFolder, pathError);
            if (pathError || relative.empty() || *relative.begin() == "..")
                continue;
            auto iterator = relative.begin();
            const auto first = *iterator;
            ++iterator;
            if (iterator != relative.end())
            {
                childFolders.insert(m_assetFolder / first);
                continue;
            }
        }
        visibleAssets.push_back(&asset);
    }

    ImGui::Separator();
    const float browserHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - 145.0f);
    ImGui::BeginChild("AssetBrowserItems", {0.0f, browserHeight}, ImGuiChildFlags_Borders);
    if (m_assetViewMode == AssetViewMode::List)
    {
        for (const auto &folder : childFolders)
        {
            const std::string label = std::string("[ ") + Localize("Folder", "文件夹") + " ]  " + folder.filename().string();
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_assetFolder = folder;
        }
        if (ImGui::BeginTable("AssetList", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn(Localize("Name", "名称"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Localize("Type", "类型"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(Localize("Size", "大小"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();
            for (const AssetInfo *asset : visibleAssets)
            {
                ImGui::PushID(asset->id.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string name = std::filesystem::path(asset->path).filename().string();
                const bool isSelected = std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) != m_selectedAssets.end();
                if (ImGui::Selectable(name.c_str(), isSelected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    selectAsset(asset->path);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        if (asset->type == "Scene")
                            OpenSceneDocument(asset->path);
                        else
                            PlaceAssetInScene(*asset);
                    }
                }
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset->path.c_str(), asset->path.size() + 1);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("AssetContext"))
                {
                    if ((asset->type == "Prefab" || asset->type == "Mesh") &&
                        ImGui::MenuItem(Localize("Place in Scene", "放入场景"), nullptr, false, !m_engine.IsPlaying()))
                        PlaceAssetInScene(*asset);
                    if (ImGui::MenuItem(Localize("Move / Rename...", "移动 / 重命名...")))
                    {
                        moveSource = asset->path;
                        std::fill(moveDestination.begin(), moveDestination.end(), '\0');
                        std::strncpy(moveDestination.data(), asset->path.c_str(), moveDestination.size() - 1);
                        ImGui::OpenPopup(Localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"));
                    }
                    const std::string regenerateLabel =
                        m_selectedAssets.size() > 1 ? std::string(Localize("Regenerate Selected Thumbnails (", "重新生成所选缩略图（")) +
                                                          std::to_string(m_selectedAssets.size()) + Localize(")", "）")
                                                    : Localize("Regenerate Thumbnail", "重新生成缩略图");
                    if (ImGui::MenuItem(regenerateLabel.c_str()))
                        regenerateSelectedThumbnails(*asset);
                    ImGui::EndPopup();
                }
                ImGui::TableSetColumnIndex(1);
                const std::string type = LocalizeReflected(asset->type);
                ImGui::TextUnformatted(type.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu KB", static_cast<unsigned long long>((asset->byteSize + 1023) / 1024));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    else
    {
        const float tileWidth = m_assetThumbnailSize + 24.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / tileWidth));
        int column = 0;
        for (const auto &folder : childFolders)
        {
            ImGui::PushID(folder.generic_string().c_str());
            const ImVec2 size{m_assetThumbnailSize, m_assetThumbnailSize + 32.0f};
            if (ImGui::Button((std::string(Localize("Folder\n", "文件夹\n")) + ShortAssetName(folder)).c_str(), size))
                m_assetFolder = folder;
            ImGui::PopID();
            if (++column < columns)
                ImGui::SameLine();
            else
                column = 0;
        }
        for (const AssetInfo *asset : visibleAssets)
        {
            ImGui::PushID(asset->id.c_str());
            RequestThumbnail(*asset);
            const auto thumbnailPath = GetThumbnailPath(*asset);
            const bool thumbnailReady = IsThumbnailReady(*asset, thumbnailPath);
            const std::uintptr_t texture = thumbnailReady ? m_device->GetUiTexture(thumbnailPath.generic_string()) : 0;
            const ImVec2 tileStart = ImGui::GetCursorScreenPos();
            const ImVec2 tileSize{m_assetThumbnailSize, m_assetThumbnailSize + 34.0f};
            ImGui::InvisibleButton("AssetTile", tileSize);
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) != m_selectedAssets.end();
            ImDrawList *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(tileStart, {tileStart.x + tileSize.x, tileStart.y + tileSize.y},
                                ImGui::GetColorU32(selected  ? ImGuiCol_HeaderActive
                                                   : hovered ? ImGuiCol_HeaderHovered
                                                             : ImGuiCol_FrameBg),
                                4.0f);
            const ImVec2 imageMaximum{tileStart.x + m_assetThumbnailSize, tileStart.y + m_assetThumbnailSize};
            if (texture)
                draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), tileStart, imageMaximum);
            else
            {
                draw->AddRectFilled({tileStart.x + 8.0f, tileStart.y + 8.0f}, {imageMaximum.x - 8.0f, imageMaximum.y - 8.0f},
                                    IM_COL32(40, 79, 125, 255), 6.0f);
                const std::string type = LocalizeReflected(asset->type);
                draw->AddText({tileStart.x + 12.0f, tileStart.y + m_assetThumbnailSize * 0.45f}, IM_COL32_WHITE, type.c_str());
            }
            const std::string name = ShortAssetName(asset->path);
            draw->AddText({tileStart.x + 4.0f, imageMaximum.y + 7.0f}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (ImGui::IsItemClicked())
                selectAsset(asset->path);
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (asset->type == "Scene")
                    OpenSceneDocument(asset->path);
                else
                    PlaceAssetInScene(*asset);
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset->path.c_str(), asset->path.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::TextDisabled("%s", Localize("Drop into Scene View", "拖到场景视图中"));
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("AssetContext"))
            {
                if ((asset->type == "Prefab" || asset->type == "Mesh") &&
                    ImGui::MenuItem(Localize("Place in Scene", "放入场景"), nullptr, false, !m_engine.IsPlaying()))
                    PlaceAssetInScene(*asset);
                if (ImGui::MenuItem(Localize("Move / Rename...", "移动 / 重命名...")))
                {
                    moveSource = asset->path;
                    std::fill(moveDestination.begin(), moveDestination.end(), '\0');
                    std::strncpy(moveDestination.data(), asset->path.c_str(), moveDestination.size() - 1);
                    ImGui::OpenPopup(Localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"));
                }
                const std::string regenerateLabel =
                    m_selectedAssets.size() > 1 ? std::string(Localize("Regenerate Selected Thumbnails (", "重新生成所选缩略图（")) +
                                                      std::to_string(m_selectedAssets.size()) + Localize(")", "）")
                                                : Localize("Regenerate Thumbnail", "重新生成缩略图");
                if (ImGui::MenuItem(regenerateLabel.c_str()))
                    regenerateSelectedThumbnails(*asset);
                ImGui::EndPopup();
            }
            if (hovered)
                ImGui::SetTooltip("%s\n%s", asset->path.c_str(), LocalizeReflected(asset->type).c_str());
            ImGui::PopID();
            if (++column < columns)
                ImGui::SameLine();
            else
                column = 0;
        }
    }
    if (visibleAssets.empty() && childFolders.empty())
        ImGui::TextDisabled("%s", Localize("No matching assets", "没有匹配的资产"));
    ImGui::EndChild();
    if (ImGui::BeginPopupModal(Localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(Localize("Move: %s", "移动：%s"), moveSource.c_str());
        ImGui::InputText(Localize("Destination Path", "目标路径"), moveDestination.data(), moveDestination.size());
        if (ImGui::Button(Localize("Move", "移动")))
        {
            std::string error;
            auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
            if (AssetMaintenance::MoveAsset(registry, moveSource, moveDestination.data(), &error))
            {
                m_selectedAsset = moveDestination.data();
                m_selectedAssets = {m_selectedAsset};
                m_messages.emplace_back(Localize("Asset moved and references updated", "资产已移动，引用已更新"));
                ImGui::CloseCurrentPopup();
            }
            else
                m_messages.emplace_back(std::string(Localize("Move failed: ", "移动失败：")) + error);
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const auto preview = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                 : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    if (preview)
    {
        ImGui::SeparatorText(Localize("Preview and Dependencies", "预览与依赖"));
        RequestThumbnail(*preview);
        const auto previewThumbnail = GetThumbnailPath(*preview);
        if (IsThumbnailReady(*preview, previewThumbnail))
        {
            const std::uintptr_t texture = m_device->GetUiTexture(previewThumbnail.generic_string());
            if (texture)
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), {96.0f, 96.0f});
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        const std::string previewTypeLabel = LocalizeReflected(preview->type);
        ImGui::Text(Localize("Type: %s", "类型：%s"), previewTypeLabel.c_str());
        ImGui::Text(Localize("Size: %llu bytes", "大小：%llu 字节"), static_cast<unsigned long long>(preview->byteSize));
        ImGui::Text(Localize("Dependencies: %zu", "依赖：%zu"), preview->dependencies.size());
        if ((preview->type == "Prefab" || preview->type == "Mesh") && !m_engine.IsPlaying() &&
            ImGui::Button(Localize("Place in Scene", "放入场景")))
            PlaceAssetInScene(*preview);
        ImGui::SameLine();
        if (ImGui::SmallButton(Localize("Regenerate Thumbnail", "重新生成缩略图")))
            RequestThumbnail(*preview, true);
        ImGui::EndGroup();
        auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
        for (const auto &issue : AssetMaintenance::FindMissingDependencies(registry))
            if (issue.owner == preview->path)
            {
                ImGui::TextColored({1.0f, 0.45f, 0.3f, 1.0f}, Localize("Missing: %s", "缺失：%s"), issue.missingPath.c_str());
                for (const auto &candidate : issue.candidates)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton((std::string(Localize("Repair as -> ", "修复为 → ")) + candidate).c_str()))
                    {
                        std::string error;
                        if (!AssetMaintenance::RepairDependency(registry, issue.owner, issue.missingPath, candidate, &error))
                            m_messages.emplace_back(std::string(Localize("Dependency repair failed: ", "依赖修复失败：")) + error);
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
            m_messages.emplace_back(std::string(Localize("Unable to open animation asset: ", "无法打开动画资产：")) + m_selectedAsset);
            return;
        }
        m_openAnimationAsset = m_selectedAsset;
    }

    ImGui::SetNextWindowSize({720.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Localize("Animation Graph / Events", "动画图 / 动画事件")))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", m_openAnimationAsset.c_str());
    ImGui::Separator();
    if (extension == ".skanim")
    {
        ImGui::Text(Localize("Clip: %s | %.3f seconds | %zu bones", "片段：%s | %.3f 秒 | %zu 根骨骼"), m_editSkeletalClip.name.c_str(),
                    m_editSkeletalClip.duration,
                    m_editSkeletalClip.IsCompressed()      ? m_editSkeletalClip.tracks.size()
                    : m_editSkeletalClip.keyframes.empty() ? 0
                                                           : m_editSkeletalClip.keyframes.front().localPose.size());
        ImGui::Text(Localize("Storage: %s", "存储：%s"), m_editSkeletalClip.IsCompressed() ? Localize("Compressed Curves", "压缩曲线")
                                                                                           : Localize("Raw Sampled Poses", "原始采样姿势"));
        static float debugTime = 0.0f;
        ImGui::SliderFloat(Localize("Debug Time", "调试时间"), &debugTime, 0.0f, m_editSkeletalClip.duration);
        const auto skeletonPath = std::filesystem::path(m_openAnimationAsset).replace_extension(".skeleton");
        Skeleton debugSkeleton;
        if (LoadSkeletonAsset(skeletonPath, debugSkeleton))
        {
            const auto snapshot =
                BuildAnimationDebugSnapshot(debugSkeleton, m_editSkeletalClip.Sample(debugTime, debugSkeleton.bones.size(), false));
            ImGui::Text(Localize("Debug Pose: %zu joints / %zu links | Root Motion %.2f, %.2f, %.2f",
                                 "调试姿势：%zu 个关节 / %zu 条连接 | 根运动 %.2f, %.2f, %.2f"),
                        snapshot.jointPositions.size(), snapshot.bones.size(), m_editSkeletalClip.SampleRootMotion(debugTime, false).x,
                        m_editSkeletalClip.SampleRootMotion(debugTime, false).y, m_editSkeletalClip.SampleRootMotion(debugTime, false).z);
        }
        else
            ImGui::TextDisabled("%s", Localize("Place a matching .skeleton file beside the clip to preview joint debug data.",
                                               "请在片段旁放置同名 .skeleton 文件以预览关节调试信息"));
        ImGui::BeginDisabled(m_editSkeletalClip.IsCompressed());
        if (ImGui::Button(Localize("Compress Curves", "压缩曲线")))
        {
            const auto stats = m_editSkeletalClip.Compress();
            m_messages.emplace_back(std::string(Localize("Animation curve keys: ", "动画曲线关键帧：")) +
                                    std::to_string(stats.originalCurveKeys) + " -> " + std::to_string(stats.compressedCurveKeys));
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText(Localize("Events", "事件"));
        int remove = -1;
        for (std::size_t index = 0; index < m_editSkeletalClip.events.size(); ++index)
        {
            auto &event = m_editSkeletalClip.events[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat(Localize("Time", "时间"), &event.time, 0.01f, 0.0f, m_editSkeletalClip.duration);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            InputString(Localize("Name", "名称"), event.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            InputString(Localize("Payload", "载荷"), event.payload, 256);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                remove = static_cast<int>(index);
            ImGui::PopID();
        }
        if (remove >= 0)
            m_editSkeletalClip.events.erase(m_editSkeletalClip.events.begin() + remove);
        if (ImGui::Button(Localize("+ Event", "+ 事件")))
            m_editSkeletalClip.events.push_back({0.0f, "Event", {}});
        ImGui::SameLine();
        if (ImGui::Button(Localize("Save Clip", "保存片段")))
        {
            std::sort(m_editSkeletalClip.events.begin(), m_editSkeletalClip.events.end(),
                      [](const AnimationEvent &left, const AnimationEvent &right) { return left.time < right.time; });
            if (SaveSkeletalClipAsset(m_editSkeletalClip, m_openAnimationAsset))
                m_messages.emplace_back(std::string(Localize("Animation events saved: ", "动画事件已保存：")) + m_openAnimationAsset);
        }
    }
    else if (extension == ".blendtree")
    {
        InputString(Localize("Parameter", "参数"), m_editBlendTree.parameter, 128);
        ImGui::SeparatorText(Localize("1D Samples", "一维采样点"));
        int remove = -1;
        for (std::size_t index = 0; index < m_editBlendTree.samples.size(); ++index)
        {
            auto &sample = m_editBlendTree.samples[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat(Localize("Threshold", "阈值"), &sample.threshold, 0.05f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(390.0f);
            InputString(Localize("Clip", "片段"), sample.clipPath);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                remove = static_cast<int>(index);
            ImGui::PopID();
        }
        if (remove >= 0)
            m_editBlendTree.samples.erase(m_editBlendTree.samples.begin() + remove);
        if (ImGui::Button(Localize("+ Sample", "+ 采样点")))
            m_editBlendTree.samples.push_back(
                {m_editBlendTree.samples.empty() ? 0.0f : m_editBlendTree.samples.back().threshold + 1.0f, {}});
        ImGui::SameLine();
        if (ImGui::Button(Localize("Save Blend Tree", "保存混合树")))
        {
            std::sort(m_editBlendTree.samples.begin(), m_editBlendTree.samples.end(),
                      [](const BlendTreeSample &left, const BlendTreeSample &right) { return left.threshold < right.threshold; });
            if (SaveBlendTreeAsset(m_editBlendTree, m_openAnimationAsset))
            {
                std::vector<std::string> dependencies;
                dependencies.reserve(m_editBlendTree.samples.size());
                for (const auto &sample : m_editBlendTree.samples)
                    if (std::find(dependencies.begin(), dependencies.end(), sample.clipPath) == dependencies.end())
                        dependencies.push_back(sample.clipPath);
                m_engine.GetContext().resourceManager->GetAssetRegistry().SetDependencies(m_openAnimationAsset, std::move(dependencies));
                m_messages.emplace_back(std::string(Localize("Blend tree saved: ", "混合树已保存：")) + m_openAnimationAsset);
            }
            else
                m_messages.emplace_back(Localize("Blend tree requires unique thresholds and valid animation clip paths.",
                                                 "混合树需要互不重复的阈值和有效的动画片段路径"));
        }
    }
    else
    {
        InputString(Localize("Entry State", "入口状态"), m_editAnimationGraph.entryState, 128);
        ImGui::SeparatorText(Localize("Graph Preview", "图预览"));
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

        ImGui::SeparatorText(Localize("Parameters", "参数"));
        int removeParameter = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.parameters.size(); ++index)
        {
            auto &parameter = m_editAnimationGraph.parameters[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(220.0f);
            InputString(Localize("Name", "名称"), parameter.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat(Localize("Default Value", "默认值"), &parameter.defaultValue, 0.05f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeParameter = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeParameter >= 0)
            m_editAnimationGraph.parameters.erase(m_editAnimationGraph.parameters.begin() + removeParameter);
        if (ImGui::Button(Localize("+ Parameter", "+ 参数")))
            m_editAnimationGraph.parameters.push_back({"Parameter" + std::to_string(m_editAnimationGraph.parameters.size()), 0.0f});

        ImGui::SeparatorText(Localize("States", "状态"));
        int removeState = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.states.size(); ++index)
        {
            auto &state = m_editAnimationGraph.states[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(180.0f);
            InputString(Localize("State", "状态"), state.name, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(360.0f);
            InputString(Localize("Motion", "动作"), state.motionPath);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeState = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeState >= 0)
            m_editAnimationGraph.states.erase(m_editAnimationGraph.states.begin() + removeState);
        if (ImGui::Button(Localize("+ State", "+ 状态")))
            m_editAnimationGraph.states.push_back({"State" + std::to_string(m_editAnimationGraph.states.size()), {}});

        ImGui::SeparatorText(Localize("Transitions", "过渡"));
        static const char *comparisonNames[] = {"<", "<=", ">", ">=", "==", "!="};
        int removeTransition = -1;
        for (std::size_t index = 0; index < m_editAnimationGraph.transitions.size(); ++index)
        {
            auto &transition = m_editAnimationGraph.transitions[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(100.0f);
            InputString(Localize("From", "来源"), transition.from, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            InputString(Localize("To", "目标"), transition.to, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            InputString(Localize("Parameter", "参数"), transition.parameter, 128);
            ImGui::SameLine();
            int comparison = static_cast<int>(transition.comparison);
            ImGui::SetNextItemWidth(55.0f);
            if (ImGui::Combo(Localize("Operator", "运算符"), &comparison, comparisonNames, 6))
                transition.comparison = static_cast<AnimationGraphComparison>(comparison);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat(Localize("Value", "数值"), &transition.threshold, 0.05f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(75.0f);
            ImGui::DragFloat(Localize("Blend", "混合"), &transition.blendSeconds, 0.01f, 0.0f, 10.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                removeTransition = static_cast<int>(index);
            ImGui::PopID();
        }
        if (removeTransition >= 0)
            m_editAnimationGraph.transitions.erase(m_editAnimationGraph.transitions.begin() + removeTransition);
        if (ImGui::Button(Localize("+ Transition", "+ 过渡")) && !m_editAnimationGraph.states.empty() &&
            !m_editAnimationGraph.parameters.empty())
            m_editAnimationGraph.transitions.push_back({m_editAnimationGraph.states.front().name, m_editAnimationGraph.states.back().name,
                                                        m_editAnimationGraph.parameters.front().name, AnimationGraphComparison::Greater,
                                                        0.5f, 0.15f});
        ImGui::SameLine();
        if (ImGui::Button(Localize("Save Animation Graph", "保存动画图")))
        {
            if (SaveAnimationGraphAsset(m_editAnimationGraph, m_openAnimationAsset))
            {
                std::vector<std::string> dependencies;
                dependencies.reserve(m_editAnimationGraph.states.size());
                for (const auto &state : m_editAnimationGraph.states)
                    if (std::find(dependencies.begin(), dependencies.end(), state.motionPath) == dependencies.end())
                        dependencies.push_back(state.motionPath);
                m_engine.GetContext().resourceManager->GetAssetRegistry().SetDependencies(m_openAnimationAsset, std::move(dependencies));
                m_messages.emplace_back(std::string(Localize("Animation graph saved: ", "动画图已保存：")) + m_openAnimationAsset);
            }
            else
                m_messages.emplace_back(Localize("Animation graph contains an invalid state, parameter, entry, or transition.",
                                                 "动画图包含无效状态、参数、入口或过渡"));
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
            m_messages.emplace_back(std::string(Localize("Unable to open VFX asset: ", "无法打开特效资产：")) + m_selectedAsset);
            return;
        }
        m_openEffectAsset = m_selectedAsset;
    }
    ImGui::SetNextWindowSize({520.0f, 580.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Localize("VFX Editor", "特效编辑器")))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", m_openEffectAsset.c_str());
    int capacity = static_cast<int>(std::min<std::size_t>(m_editEffect.maximumParticles, 1'000'000));
    if (ImGui::DragInt(Localize("Capacity", "容量"), &capacity, 16.0f, 1, 1'000'000))
        m_editEffect.maximumParticles = static_cast<std::size_t>(capacity);
    ImGui::DragFloat(Localize("Emission Rate", "发射速率"), &m_editEffect.emissionRate, 0.25f, 0.0f, 100000.0f);
    ImGui::DragFloat(Localize("Lifetime", "生命周期"), &m_editEffect.lifetime, 0.01f, 0.01f, 600.0f);
    ImGui::DragFloat(Localize("Speed", "速度"), &m_editEffect.speed, 0.05f, 0.0f, 1000.0f);
    ImGui::DragFloat(Localize("Spread", "扩散"), &m_editEffect.spread, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat3(Localize("Gravity", "重力"), &m_editEffect.gravity.x, 0.05f);
    ImGui::DragFloat2(Localize("Start / End Size", "起始 / 结束尺寸"), &m_editEffect.startSize, 0.01f, 0.0f, 100.0f);
    ImGui::ColorEdit4(Localize("Start Color", "起始颜色"), &m_editEffect.startColor.x);
    ImGui::ColorEdit4(Localize("End Color", "结束颜色"), &m_editEffect.endColor.x);
    InputString(Localize("Texture", "纹理"), m_editEffect.texturePath);
    int columns = m_editEffect.flipbookColumns, rows = m_editEffect.flipbookRows;
    if (ImGui::DragInt(Localize("Flipbook Columns", "序列帧列数"), &columns, 1.0f, 1, 256))
        m_editEffect.flipbookColumns = static_cast<std::uint16_t>(columns);
    if (ImGui::DragInt(Localize("Flipbook Rows", "序列帧行数"), &rows, 1.0f, 1, 256))
        m_editEffect.flipbookRows = static_cast<std::uint16_t>(rows);
    ImGui::DragFloat(Localize("Frames Per Second", "每秒帧数"), &m_editEffect.flipbookFramesPerSecond, 0.1f, 0.0f, 1000.0f);
    ImGui::Checkbox(Localize("Loop Flipbook", "循环序列帧"), &m_editEffect.flipbookLooping);
    int mode = static_cast<int>(m_editEffect.simulationMode);
    if (ImGui::Combo(Localize("Simulation Mode", "模拟方式"), &mode, "CPU\0GPU\0"))
        m_editEffect.simulationMode = static_cast<ParticleSimulationMode>(mode);
    if (ImGui::Button(Localize("Save VFX", "保存特效")))
    {
        if (SaveParticleEffectAsset(m_editEffect, m_openEffectAsset))
            m_messages.emplace_back(std::string(Localize("VFX saved: ", "特效已保存：")) + m_openEffectAsset);
        else
            m_messages.emplace_back(
                Localize("VFX contains an invalid capacity, lifetime, or flipbook size.", "特效包含无效的容量、生命周期或序列帧尺寸"));
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
            m_messages.emplace_back(std::string(Localize("Unable to open behavior tree: ", "无法打开行为树：")) + m_selectedAsset);
            return;
        }
        m_openBehaviorAsset = m_selectedAsset;
    }
    ImGui::SetNextWindowSize({760.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Localize("Behavior Tree Editor", "行为树编辑器")))
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
        ImGui::Button((LocalizeReflected(node.label) + "\n" + LocalizeReflected(node.type)).c_str(), {130.0f, 50.0f});
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            node.editorPosition.x += ImGui::GetIO().MouseDelta.x;
            node.editorPosition.y += ImGui::GetIO().MouseDelta.y;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + 340.0f});
    ImGui::Dummy({700.0f, 1.0f});
    ImGui::SeparatorText(Localize("Nodes", "节点"));
    int remove = -1;
    for (std::size_t index = 0; index < m_editBehaviorGraph.nodes.size(); ++index)
    {
        auto &node = m_editBehaviorGraph.nodes[index];
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Text("#%u", node.id);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        InputString(Localize("Type", "类型"), node.type, 64);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        InputString(Localize("Label", "标签"), node.label, 128);
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
    if (ImGui::Button(Localize("+ Action Node", "+ 动作节点")))
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
    if (ImGui::Button(Localize("Save Behavior Tree", "保存行为树")))
    {
        if (SaveBehaviorTreeGraph(m_editBehaviorGraph, m_openBehaviorAsset))
            m_messages.emplace_back(std::string(Localize("Behavior tree saved: ", "行为树已保存：")) + m_openBehaviorAsset);
        else
            m_messages.emplace_back(
                Localize("Behavior tree contains a cycle, invalid root, or missing child node.", "行为树存在循环、无效根节点或缺失子节点"));
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
    if (!ImGui::Begin(Localize("Shader Source Editor", "着色器源码编辑器")))
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
    if (ImGui::Button(Localize("Save and Reload", "保存并重载")))
    {
        PlatformFile::WriteAllText(asset->path, m_shaderSource.data());
        if (m_device->ValidateShader(asset->path) && m_device->ReloadShaders())
            m_messages.emplace_back(std::string(Localize("Shader saved and reloaded: ", "着色器已保存并重载：")) + asset->path);
        else
            m_messages.emplace_back(std::string(Localize("Shader error: ", "着色器错误：")) + m_device->GetLastShaderError());
    }
    ImGui::SameLine();
    if (m_device->GetLastShaderError().empty())
        ImGui::TextColored({0.35f, 0.9f, 0.45f, 1.0f}, "%s", Localize("No compilation errors", "没有编译错误"));
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
            ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, Localize("Line %d: %s", "第 %d 行：%s"), line, error.c_str());
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
    ImGui::Begin(Localize("Material Graph", "材质图"));
    if (ImGui::Button(Localize("+ Constant", "+ 常量")))
        m_materialGraph.AddNode(MaterialNodeType::Constant, "Constant", {30.0f, 60.0f});
    ImGui::SameLine();
    if (ImGui::Button(Localize("+ Texture", "+ 纹理")))
        m_materialGraph.AddNode(MaterialNodeType::Texture, "Texture", {30.0f, 190.0f});
    ImGui::SameLine();
    if (ImGui::Button(Localize("+ Multiply", "+ 乘法")))
        m_materialGraph.AddNode(MaterialNodeType::Multiply, "Multiply", {280.0f, 100.0f});
    ImGui::SameLine();
    if (ImGui::Button(Localize("+ Output", "+ 输出")))
        m_materialGraph.AddNode(MaterialNodeType::PbrOutput, "PBR Output", {540.0f, 100.0f});
    ImGui::SameLine();
    if (ImGui::Button(Localize("Save and Compile", "保存并编译")))
    {
        std::vector<std::string> errors;
        const std::string shader = m_materialGraph.Compile(&errors);
        const std::filesystem::path output = std::filesystem::path(asset->path).replace_extension(".frag");
        if (m_materialGraph.Save(asset->path) && !shader.empty() && PlatformFile::WriteAllText(output, shader))
            m_messages.emplace_back(std::string(Localize("Material graph compiled: ", "材质图已编译：")) + output.generic_string());
        for (const auto &error : errors)
            m_messages.emplace_back(std::string(Localize("Material graph: ", "材质图：")) + error);
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
        const std::string nodeLabel = LocalizeReflected(node.name);
        drawList->AddText({minimum.x + 8.0f, minimum.y + 8.0f}, IM_COL32_WHITE, nodeLabel.c_str());
        drawList->AddText({minimum.x + 8.0f, minimum.y + 34.0f}, IM_COL32(175, 190, 210, 255),
                          Localize("Shift + Click: Connect", "Shift + 单击：连接"));
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
    ImGui::Begin(Localize("Console / Stats", "控制台 / 统计"));
    const Renderer *renderer = m_engine.GetContext().renderer;
    if (ImGui::BeginTable("RendererStats", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        const auto stat = [](const char *label, const std::string &value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value.c_str());
        };
        stat(Localize("Draw Calls", "绘制调用"), std::to_string(renderer->GetLastDrawCallCount()));
        stat(Localize("Culled / LOD", "剔除 / LOD"),
             std::to_string(renderer->GetLastCulledCount()) + " / " + std::to_string(renderer->GetLastLodSwitchCount()));
        std::ostringstream gpu;
        gpu << std::fixed << std::setprecision(2) << m_device->GetLastGpuFrameMilliseconds() << " ms";
        stat("GPU", gpu.str());
    const auto &profile = renderer->GetLastFrameProfile();
        std::ostringstream cpu;
        cpu << std::fixed << std::setprecision(2) << profile.totalMilliseconds << " ms (" << Localize("Queue", "队列") << ' '
            << profile.queueMilliseconds << " | " << Localize("Shadow", "阴影") << ' ' << profile.shadowMilliseconds << " | "
            << Localize("Scene/UI", "场景/UI") << ' ' << profile.sceneMilliseconds << ')';
        stat("CPU", cpu.str());
        stat(Localize("Render Graph", "渲染图"), std::to_string(profile.graphPasses) + Localize(" passes / ", " 个 Pass / ") +
                                                       std::to_string(profile.graphPhysicalResources) +
                                                       Localize(" physical resources", " 个物理资源"));
        stat(Localize("Cluster References", "光簇引用"), std::to_string(profile.clusteredLightReferences));
        stat("HZB", std::to_string(m_device->GetHzbLevelCount()) + Localize(" levels", " 级"));
        stat(Localize("GPU Driven", "GPU 驱动"),
             m_device->IsGpuDrivenCullingSupported()
                 ? "Compute + Indirect"
                 : (m_device->IsIndirectDrawSupported() ? Localize("CPU Culling + Indirect Draw", "CPU 剔除 + 间接绘制")
                                                        : Localize("Fallback Path", "回退路径")));
        ImGui::EndTable();
    }
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
    std::size_t requestedClose = InvalidSceneDocument;
    for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
    {
        const auto &document = m_sceneDocuments[index];
        const std::string label = (document.path.empty() ? Localize("Untitled", "未命名") : document.path.filename().string()) + "##Scene" +
                                  std::to_string(index);
        ImGuiTabItemFlags flags =
            (index == m_activeSceneDocument ? m_sceneDirty : document.dirty) ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
        if (index == m_pendingSelectedSceneDocument)
            flags |= ImGuiTabItemFlags_SetSelected;
        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags))
        {
            if (index != m_activeSceneDocument)
                SwitchSceneDocument(index);
            if (index == m_pendingSelectedSceneDocument)
                m_pendingSelectedSceneDocument = InvalidSceneDocument;
            ImGui::EndTabItem();
        }
        if (!open)
            requestedClose = index;
    }
    ImGui::EndTabBar();
    if (requestedClose != InvalidSceneDocument)
    {
        if (requestedClose == m_activeSceneDocument)
        {
            m_sceneDocuments[requestedClose].snapshot = m_engine.GetSceneManager().CreateSnapshot();
            m_sceneDocuments[requestedClose].dirty = m_sceneDirty;
        }
        if (m_sceneDocuments[requestedClose].dirty)
        {
            m_sceneCloseDocument = requestedClose;
            m_requestSceneClosePopup = true;
        }
        else
            CloseSceneDocument(requestedClose);
    }
}

bool EditorApp::CloseSceneDocument(const std::size_t index)
{
    if (m_engine.IsPlaying() || index >= m_sceneDocuments.size())
        return false;
    const bool closingActive = index == m_activeSceneDocument;
    m_sceneDocuments.erase(m_sceneDocuments.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_sceneDocuments.empty())
    {
        m_engine.GetSceneManager().NewScene();
        m_sceneDocuments.push_back({{}, m_engine.GetSceneManager().CreateSnapshot(), false});
        m_activeSceneDocument = 0;
        m_pendingSelectedSceneDocument = 0;
        m_sceneDirty = false;
    }
    else
    {
        if (index < m_activeSceneDocument)
            --m_activeSceneDocument;
        else if (closingActive)
            m_activeSceneDocument = std::min(index, m_sceneDocuments.size() - 1);
        if (closingActive)
        {
            if (!m_engine.GetSceneManager().RestoreSnapshot(m_sceneDocuments[m_activeSceneDocument].snapshot))
                return false;
            m_sceneDirty = m_sceneDocuments[m_activeSceneDocument].dirty;
            m_pendingSelectedSceneDocument = m_activeSceneDocument;
        }
    }
    m_selected = InvalidEntity;
    m_selection.clear();
    m_undo.clear();
    m_redo.clear();
    return true;
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
        if (SamePath(m_sceneDocuments[index].path, normalized))
        {
            if (index != m_activeSceneDocument && !SwitchSceneDocument(index))
                return false;
            m_pendingSelectedSceneDocument = index;
            return true;
        }
    auto &current = m_sceneDocuments[m_activeSceneDocument];
    current.snapshot = m_engine.GetSceneManager().CreateSnapshot();
    current.dirty = m_sceneDirty;
    if (!SceneSerializer::Load(*m_engine.GetContext().world, normalized))
    {
        m_engine.GetSceneManager().RestoreSnapshot(current.snapshot);
        m_messages.emplace_back(std::string(Localize("Failed to open scene: ", "场景打开失败：")) + normalized.generic_string());
        return false;
    }
    m_sceneDocuments.push_back({normalized, m_engine.GetSceneManager().CreateSnapshot(), false});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    m_sceneDirty = false;
    m_selected = InvalidEntity;
    m_undo.clear();
    m_redo.clear();
    m_messages.emplace_back(std::string(Localize("Opened scene tab: ", "已打开场景标签：")) + normalized.generic_string());
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
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    m_sceneDirty = true;
    m_selected = InvalidEntity;
    m_undo.clear();
    m_redo.clear();
}

// 当前实现使用完整场景快照换取可靠性；记录新操作后必须清空 redo 分支。
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
    if (m_engine.IsPlaying() || m_activeSceneDocument >= m_sceneDocuments.size())
        return false;
    const auto &document = m_sceneDocuments[m_activeSceneDocument];
    if (document.path.empty())
    {
        if (RequestSaveCurrentSceneAs())
            m_messages.emplace_back(Localize("Choose a path before saving this untitled scene.",
                                             "未命名场景必须先选择保存路径。"));
        return false;
    }
    return SaveSceneDocumentTo(m_activeSceneDocument, document.path);
}

bool EditorApp::RequestSaveCurrentSceneAs()
{
    if (m_engine.IsPlaying() || m_activeSceneDocument >= m_sceneDocuments.size())
        return false;
    if (m_sceneSaveDocument != InvalidSceneDocument)
        return true;

    const auto &project = m_engine.GetProjectSettings();
    std::filesystem::path suggested = m_sceneDocuments[m_activeSceneDocument].path;
    if (suggested.empty())
    {
        const auto assetRoot = project.Resolve(project.assetDirectory);
        suggested = assetRoot / "Untitled.scene";
        for (std::size_t suffix = 2; std::filesystem::exists(suggested); ++suffix)
            suggested = assetRoot / ("Untitled" + std::to_string(suffix) + ".scene");
    }

    std::error_code error;
    const auto projectRoot = project.projectFile.parent_path();
    const auto relative = std::filesystem::relative(suggested, projectRoot, error);
    const std::string displayPath = !error && !relative.empty() ? relative.generic_string() : suggested.generic_string();
    m_sceneSavePath.fill('\0');
    std::strncpy(m_sceneSavePath.data(), displayPath.c_str(), m_sceneSavePath.size() - 1);
    m_sceneSaveDocument = m_activeSceneDocument;
    m_requestSceneSavePopup = true;
    m_packageAfterSceneSave = false;
    m_sceneSaveError.clear();
    m_sceneOverwriteTarget.clear();
    return true;
}

std::filesystem::path EditorApp::ResolveSceneSavePath(std::string &error)
{
    error.clear();
    std::string input = m_sceneSavePath.data();
    const auto first = input.find_first_not_of(" \t\r\n");
    const auto last = input.find_last_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        error = Localize("Enter a project-relative scene path.", "请输入项目相对场景路径。");
        return {};
    }
    input = input.substr(first, last - first + 1);

    std::filesystem::path requested = std::filesystem::u8path(input);
    if (!requested.has_extension())
        requested.replace_extension(".scene");
    if (Lowercase(requested.extension().string()) != ".scene")
    {
        error = Localize("Scene files must use the .scene extension.", "场景文件必须使用 .scene 扩展名。");
        return {};
    }

    const auto &project = m_engine.GetProjectSettings();
    if (project.projectFile.empty())
    {
        error = Localize("Open a project before saving a scene.", "保存场景前必须先打开项目。");
        return {};
    }
    std::filesystem::path target = requested.is_absolute() ? requested : project.Resolve(requested);
    std::error_code pathError;
    target = std::filesystem::weakly_canonical(target, pathError);
    if (pathError)
    {
        error = Localize("The scene path could not be resolved.", "无法解析场景保存路径。");
        return {};
    }
    const auto assetRoot = std::filesystem::weakly_canonical(project.Resolve(project.assetDirectory), pathError);
    if (pathError)
    {
        error = Localize("The project asset directory is unavailable.", "项目资产目录不可用。");
        return {};
    }
    const auto assetRelative = std::filesystem::relative(target, assetRoot, pathError);
    bool outsideAssets = pathError || assetRelative.empty() || assetRelative.is_absolute();
    if (!outsideAssets)
        for (const auto &component : assetRelative)
            if (component == "..")
            {
                outsideAssets = true;
                break;
            }
    if (outsideAssets)
    {
        error = Localize("Save scenes inside the project's asset directory.", "场景必须保存在项目资产目录内。");
        return {};
    }
    if (std::filesystem::exists(target, pathError) && !std::filesystem::is_regular_file(target, pathError))
    {
        error = Localize("The selected scene path is not a regular file.", "所选场景路径不是普通文件。");
        return {};
    }
    for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
        if (index != m_sceneSaveDocument && SamePath(m_sceneDocuments[index].path, target))
        {
            error = Localize("Another open scene tab already uses this path.", "另一个已打开的场景标签正在使用此路径。");
            return {};
        }
    return target;
}

bool EditorApp::SaveSceneDocumentTo(const std::size_t documentIndex, const std::filesystem::path &path)
{
    if (m_engine.IsPlaying() || documentIndex >= m_sceneDocuments.size() || documentIndex != m_activeSceneDocument || path.empty())
        return false;
    const auto normalized = path.lexically_normal();
    const bool saved = m_engine.SaveScene(normalized);
    if (saved)
    {
        m_sceneDirty = false;
        auto &document = m_sceneDocuments[documentIndex];
        document.path = normalized;
        document.snapshot = m_engine.GetSceneManager().CreateSnapshot();
        document.dirty = false;
        std::error_code error;
        std::filesystem::remove(GetAutosavePath(), error);
        m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(
            m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
    }
    m_messages.emplace_back(saved ? std::string(Localize("Scene saved: ", "场景已保存：")) + normalized.generic_string()
                                  : Localize("Failed to save scene", "场景保存失败"));
    return saved;
}

void EditorApp::DrawSceneSaveDialog()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({1.0f, 1.0f}, ImGuiCond_Always);
    ImGui::Begin("##SceneSaveDialogHost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    const char *popupName = Localize("Save Scene As###SceneSaveDialog", "场景另存为###SceneSaveDialog");
    if (m_requestSceneSavePopup)
    {
        ImGui::OpenPopup(popupName);
        m_requestSceneSavePopup = false;
    }

    bool packageNow = false;
    std::size_t closeAfterSave = InvalidSceneDocument;
    if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(Localize("Project-relative path inside the asset directory:", "请输入资产目录内的项目相对路径："));
    ImGui::SetNextItemWidth(520.0f);
    if (ImGui::InputText("##SceneSavePath", m_sceneSavePath.data(), m_sceneSavePath.size()))
    {
        m_sceneSaveError.clear();
        m_sceneOverwriteTarget.clear();
    }
    ImGui::TextDisabled("%s", Localize("Example: assets/Scenes/Level01.scene", "示例：assets/Scenes/Level01.scene"));

    const auto resetDialog = [this]() {
        m_sceneSaveDocument = InvalidSceneDocument;
        m_sceneSaveError.clear();
        m_sceneOverwriteTarget.clear();
        m_packageAfterSceneSave = false;
        m_closeAfterSceneSave = false;
    };
    const auto completeSave = [this, &packageNow, &closeAfterSave, &resetDialog]() {
        packageNow = m_packageAfterSceneSave;
        closeAfterSave = m_closeAfterSceneSave ? m_sceneSaveDocument : InvalidSceneDocument;
        resetDialog();
        ImGui::CloseCurrentPopup();
    };

    if (!m_sceneSaveError.empty())
        ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s", m_sceneSaveError.c_str());
    if (!m_sceneOverwriteTarget.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", Localize("This file already exists. Overwriting keeps the previous contents in a .bak file.",
                                          "该文件已存在。覆盖前会把旧内容保存在 .bak 文件中。"));
        if (ImGui::Button(Localize("Overwrite", "确认覆盖")))
        {
            std::string validationError;
            const auto currentTarget = ResolveSceneSavePath(validationError);
            if (validationError.empty() && SamePath(currentTarget, m_sceneOverwriteTarget) &&
                SaveSceneDocumentTo(m_sceneSaveDocument, currentTarget))
                completeSave();
            else
                m_sceneSaveError = validationError.empty() ? Localize("The scene could not be saved.", "场景保存失败。") : validationError;
        }
        ImGui::SameLine();
    }
    else if (ImGui::Button(Localize("Save", "保存")))
    {
        const auto target = ResolveSceneSavePath(m_sceneSaveError);
        if (!target.empty())
        {
            const auto currentPath = m_sceneSaveDocument < m_sceneDocuments.size() ? m_sceneDocuments[m_sceneSaveDocument].path
                                                                                   : std::filesystem::path{};
            if (std::filesystem::is_regular_file(target) && !SamePath(currentPath, target))
                m_sceneOverwriteTarget = target;
            else if (SaveSceneDocumentTo(m_sceneSaveDocument, target))
                completeSave();
            else
                m_sceneSaveError = Localize("The scene could not be saved.", "场景保存失败。");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Localize("Cancel", "取消")))
    {
        if (m_packageAfterSceneSave)
            m_messages.emplace_back(Localize("Packaging canceled because Save As was canceled.", "已取消另存为，因此没有执行打包。"));
        resetDialog();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::End();

    if (packageNow)
        PackageProject();
    if (closeAfterSave != InvalidSceneDocument)
        CloseSceneDocument(closeAfterSave);
}

void EditorApp::DrawSceneCloseDialog()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({1.0f, 1.0f}, ImGuiCond_Always);
    ImGui::Begin("##SceneCloseDialogHost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    const char *popupName = Localize("Save changes before closing?###SceneCloseDialog", "关闭前保存更改？###SceneCloseDialog");
    if (m_requestSceneClosePopup)
    {
        ImGui::OpenPopup(popupName);
        m_requestSceneClosePopup = false;
    }
    if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    if (m_sceneCloseDocument >= m_sceneDocuments.size())
    {
        m_sceneCloseDocument = InvalidSceneDocument;
        ImGui::CloseCurrentPopup();
    }
    else
    {
        const auto &document = m_sceneDocuments[m_sceneCloseDocument];
        const std::string name = document.path.empty() ? Localize("Untitled", "未命名") : document.path.filename().string();
        ImGui::TextWrapped(Localize("Save changes to '%s' before closing it?", "关闭“%s”前要保存更改吗？"), name.c_str());
        if (ImGui::Button(Localize("Save", "保存")))
        {
            const std::size_t target = m_sceneCloseDocument;
            if (target != m_activeSceneDocument && !SwitchSceneDocument(target))
                m_messages.emplace_back(Localize("Unable to activate the scene before saving.", "保存前无法切换到该场景。"));
            else if (m_sceneDocuments[target].path.empty())
            {
                if (RequestSaveCurrentSceneAs())
                    m_closeAfterSceneSave = true;
                m_sceneCloseDocument = InvalidSceneDocument;
                ImGui::CloseCurrentPopup();
            }
            else if (SaveSceneDocumentTo(target, m_sceneDocuments[target].path))
            {
                CloseSceneDocument(target);
                m_sceneCloseDocument = InvalidSceneDocument;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("Discard", "不保存")))
        {
            CloseSceneDocument(m_sceneCloseDocument);
            m_sceneCloseDocument = InvalidSceneDocument;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消")))
        {
            m_sceneCloseDocument = InvalidSceneDocument;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
    ImGui::End();
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
        m_messages.emplace_back(std::string(Localize("Recovery scene autosaved: ", "恢复场景已自动保存：")) + path.generic_string());
    else
        m_messages.emplace_back(std::string(Localize("Autosave failed: ", "自动保存失败：")) + path.generic_string());
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
    m_messages.emplace_back(loaded ? Localize("Editor autosave restored", "编辑器自动保存已恢复")
                                   : Localize("Failed to restore editor autosave", "编辑器自动保存恢复失败"));
    return loaded;
}

void EditorApp::UpdateWindowTitle()
{
    if (!m_device || (m_titleInitialized && m_lastTitleDirty == m_sceneDirty))
        return;
    const std::string title = std::string(Localize("TinyEngine Editor - ", "TinyEngine 编辑器 - ")) + m_engine.GetProjectSettings().name +
                              (m_sceneDirty ? " *" : "");
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), title.c_str());
    m_lastTitleDirty = m_sceneDirty;
    m_titleInitialized = true;
}
} // namespace MiniEngine
