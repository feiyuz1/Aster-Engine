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
#include "MiniEngine/Project/ProjectScaffolder.hpp"
#include "MiniEngine/Project/SolutionGenerator.hpp"
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
#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <future>
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

Vec3 MatrixColumn(const Mat4 &matrix, int column)
{
    return {matrix.At(0, column), matrix.At(1, column), matrix.At(2, column)};
}

bool ToLocalVector(const Mat4 &parent, const Vec3 &worldVector, Vec3 &local)
{
    const Vec3 x = MatrixColumn(parent, 0), y = MatrixColumn(parent, 1), z = MatrixColumn(parent, 2);
    const float determinant = Dot(x, Cross(y, z));
    if (std::abs(determinant) < 1e-8f) return false;
    local = {Dot(worldVector, Cross(y, z)) / determinant, Dot(worldVector, Cross(z, x)) / determinant,
             Dot(worldVector, Cross(x, y)) / determinant};
    return true;
}

Vec3 RotationEuler(const Mat4 &rotation)
{
    const float y = std::asin(std::clamp(-rotation.At(2, 0), -1.0f, 1.0f));
    const float x = std::abs(std::cos(y)) > 1e-5f ? std::atan2(rotation.At(2, 1), rotation.At(2, 2))
                                                : std::atan2(-rotation.At(1, 2), rotation.At(1, 1));
    const float z = std::abs(std::cos(y)) > 1e-5f ? std::atan2(rotation.At(1, 0), rotation.At(0, 0)) : 0.0f;
    return {x * 180.0f / Pi, y * 180.0f / Pi, z * 180.0f / Pi};
}

bool RotationPlaneVector(const FrameView &view, const ImVec2 mouse, const Vec3 &origin, const Vec3 &axis,
                         float minimumX, float minimumY, float width, float height, Vec3 &radial)
{
    if (width <= 0 || height <= 0) return false;
    const float x = ((mouse.x - minimumX) / width * 2.0f - 1.0f) / view.projection.At(0, 0);
    const float y = (1.0f - (mouse.y - minimumY) / height * 2.0f) / view.projection.At(1, 1);
    const Vec3 ray = Normalize(Vec3{view.view.At(0, 0) * x + view.view.At(1, 0) * y - view.view.At(2, 0),
                                    view.view.At(0, 1) * x + view.view.At(1, 1) * y - view.view.At(2, 1),
                                    view.view.At(0, 2) * x + view.view.At(1, 2) * y - view.view.At(2, 2)});
    const float denominator = Dot(ray, axis);
    if (std::abs(denominator) < 0.04f) return false;
    const float distance = Dot(origin - view.cameraPosition, axis) / denominator;
    if (distance <= 0) return false;
    radial = view.cameraPosition + ray * distance - origin;
    if (Length(radial) < 0.001f) return false;
    radial = Normalize(radial);
    return true;
}

// 把垂直于 axis、半径 radius 的圆环投影为屏幕折线；任一采样点位于相机后方时返回空，避免错误连线。
std::vector<ImVec2> ProjectAxisRing(const FrameView &view, const Vec3 &origin, const Vec3 &axis, const float radius,
                                    const float minimumX, const float minimumY, const float width, const float height)
{
    const Vec3 helper = std::abs(axis.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 u = Normalize(Cross(axis, helper)), v = Normalize(Cross(axis, u));
    constexpr int segments = 32;
    std::vector<ImVec2> points;
    points.reserve(segments);
    for (int index = 0; index < segments; ++index)
    {
        const float angle = static_cast<float>(index) / static_cast<float>(segments) * 6.2831853f;
        ImVec2 screen;
        if (!ProjectToScreen(view, origin + (u * std::cos(angle) + v * std::sin(angle)) * radius, minimumX, minimumY, width, height,
                             screen))
            return {};
        points.push_back(screen);
    }
    return points;
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

float DistanceToPolyline(const ImVec2 point, const std::vector<ImVec2> &points, const bool closed)
{
    float best = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index + 1 < points.size(); ++index)
        best = std::min(best, DistanceToSegment(point, points[index], points[index + 1]));
    if (closed && points.size() > 2)
        best = std::min(best, DistanceToSegment(point, points.back(), points.front()));
    return best;
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

#ifdef _WIN32
std::optional<std::filesystem::path> OpenTinyProjectDialog(const HWND owner)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return {};
    IFileOpenDialog *dialog = nullptr;
    std::optional<std::filesystem::path> selected;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) && dialog)
    {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"选择 TinyEngine 项目");
        const COMDLG_FILTERSPEC filters[] = {{L"TinyEngine 项目", L"*.tinyproject"}};
        dialog->SetFileTypes(1, filters);
        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    selected = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (uninitialize)
        CoUninitialize();
    return selected;
}
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
} // 匿名命名空间

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
        else if (line.rfind("layout=", 0) == 0)
        {
            std::istringstream values(line.substr(7));
            float left, right, upper, assets;
            if (values >> left >> right >> upper >> assets && std::isfinite(left) && std::isfinite(right) &&
                std::isfinite(upper) && std::isfinite(assets))
            {
                m_leftPanelRatio = std::clamp(left, 0.14f, 0.28f);
                m_rightPanelRatio = std::clamp(right, 0.18f, 0.30f);
                m_upperPanelRatio = std::clamp(upper, 0.50f, 0.84f);
                m_assetsPanelRatio = std::clamp(assets, 0.35f, 0.75f);
            }
        }
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
    output << "layout=" << m_leftPanelRatio << ' ' << m_rightPanelRatio << ' ' << m_upperPanelRatio << ' ' << m_assetsPanelRatio << '\n';
#ifdef TINYENGINE_HAS_ASSIMP
    output << "lastImportDirectory=" << m_lastImportDirectory.generic_u8string() << '\n';
#endif
    return output.good();
}

// 编辑器复用 Engine 的 World 与服务，但以编辑态启动，并把 ImGui 回调挂到场景渲染之后。
bool EditorApp::Initialize(const std::filesystem::path &projectFile)
{
    m_projectLauncherMode = projectFile.empty();
    m_requestedProject.clear();
    if (m_projectLauncherMode)
    {
        const auto parent = std::filesystem::current_path().u8string();
        std::snprintf(m_newProjectParent.data(), m_newProjectParent.size(), "%s", parent.c_str());
    }
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
    m_engine.SetTextInputManagedByUi(false);
    if (!m_device->EnableSceneRenderTarget(true, 960, 540))
    {
        m_engine.Shutdown();
        return false;
    }
    // 编辑器接管 ESC 语义（关闭弹窗/取消选择），不再把按键直接映射为退出请求。
    m_device->SetEscapeQuits(false);
    // DPI 缩放比例用于把 ImGui 逻辑尺寸换算成场景渲染目标的物理像素，保证视口清晰。
    m_dpiScale = m_device->GetDpiScale();
    m_engine.GetContext().renderer->SetCameraOverride(true, m_editorCameraPosition, m_editorCameraRotation, m_editorCameraFov);
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), Localize("TinyEngine Editor", "TinyEngine 编辑器"));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // ImGui 逻辑坐标与 96 DPI 保持一致；高分屏清晰度由 DPI 缩放模式和字体栅格密度自动保证。
    const bool chineseFontLoaded = LoadChineseEditorFont(io);
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.FramePadding = {6, 3};
    style.ItemSpacing = {6, 4};
    style.WindowPadding = {8, 8};
    style.Colors[ImGuiCol_WindowBg] = {0.075f, 0.085f, 0.105f, 1};
    style.Colors[ImGuiCol_ChildBg] = {0.065f, 0.075f, 0.095f, 1};
    style.Colors[ImGuiCol_TitleBg] = {0.105f, 0.125f, 0.16f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.13f, 0.19f, 0.27f, 1};
    style.Colors[ImGuiCol_Button] = {0.16f, 0.21f, 0.28f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.23f, 0.36f, 0.51f, 1};
    style.Colors[ImGuiCol_FrameBg] = {0.12f, 0.15f, 0.20f, 1};
    style.Colors[ImGuiCol_Header] = {0.16f, 0.29f, 0.43f, 1};
    style.Colors[ImGuiCol_Text] = {0.89f, 0.92f, 0.96f, 1};
    style.Colors[ImGuiCol_TextDisabled] = {0.56f, 0.63f, 0.72f, 1};
    if (!ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window *>(m_device->GetNativeWindow()), m_device->GetGlContext()) ||
        !ImGui_ImplOpenGL3_Init("#version 330"))
    {
        ImGui::DestroyContext();
        m_engine.Shutdown();
        return false;
    }
    m_device->SetEventCallback([](const void *event) { ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event *>(event)); });
    m_device->SetCloseRequestCallback([this]() { RequestEditorExit(); });
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
    if (m_solutionFuture.valid())
        m_solutionFuture.wait();
    m_engine.GetContext().renderer->SetOverlayCallback({});
    m_device->SetEventCallback({});
    m_device->SetCloseRequestCallback({});
    m_device->EnableSceneRenderTarget(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_engine.Shutdown();
    m_initialized = false;
}

std::filesystem::path EditorApp::TakeRequestedProject()
{
    return std::move(m_requestedProject);
}

void EditorApp::Draw()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (m_projectLauncherMode)
    {
        DrawProjectLauncher();
        ImGui::Render();
        m_device->SetTextInputEnabled(ImGui::GetIO().WantTextInput);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }
    m_sceneCommandsFocused = false;
    m_inspectorFocused = false;
    m_shaderFocused = false;
    DrawMenuBar();
    DrawSolutionStatus();
    if (!m_sceneMaximized) DrawHierarchy();
    if (!m_sceneMaximized) DrawInspector();
    DrawSceneView();
    if (!m_sceneMaximized) DrawAssets();
    if (!m_sceneMaximized) DrawConsole();
    if (!m_sceneMaximized) DrawWorkspaceSplitters();
    DrawAnimationEditor();
    DrawShaderEditor();
    DrawMaterialGraphEditor();
    DrawEffectEditor();
    DrawBehaviorTreeEditor();
    DrawSceneOpenDialog();
    DrawSceneSaveDialog();
    DrawSceneCloseDialog();
    DrawExitDialog();
    DrawShaderCloseDialog();
    // 浮动设置窗口最后提交，保证它位于固定布局面板之上。
    DrawSettings();
    HandleShortcuts();
    if (!ImGui::IsAnyItemActive())
        m_inspectorEditId = 0;
    if (m_sceneDirty && !m_engine.IsPlaying() && ImGui::GetTime() - m_lastAutosaveTime >= 30.0)
        Autosave();
    UpdateWindowTitle();
    ImGui::Render();
    m_device->SetTextInputEnabled(ImGui::GetIO().WantTextInput);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorApp::DrawProjectLauncher()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (!ImGui::Begin("项目启动###ProjectLauncher", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        ImGui::End();
        return;
    }

    const float width = std::min(760.0f, std::max(320.0f, ImGui::GetContentRegionAvail().x - 40.0f));
    ImGui::SetCursorPosX(std::max(20.0f, (ImGui::GetWindowSize().x - width) * 0.5f));
    ImGui::BeginChild("启动内容", {width, 0}, false);
    ImGui::Dummy({0, 24});
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted(Localize("TinyEngine Projects", "TinyEngine 项目"));
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s", Localize("Create a project or open an existing one.", "新建项目，或打开已有项目。"));
    ImGui::Dummy({0, 18});

    ImGui::SeparatorText(Localize("Create Project", "新建项目"));
    ImGui::TextUnformatted(Localize("Project Name", "项目名称"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ProjectName", m_newProjectName.data(), m_newProjectName.size());
    ImGui::TextUnformatted(Localize("Parent Folder", "保存到目录"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ProjectParent", m_newProjectParent.data(), m_newProjectParent.size());
    ImGui::TextDisabled("%s", Localize("The folder must exist. The project folder is created inside it.",
                                           "此目录需已存在；项目文件夹会自动建在其中。"));
    if (ImGui::Button(Localize("Create and Open", "创建并打开")))
    {
#ifdef TINYENGINE_SOURCE_ROOT
        const std::filesystem::path engineRoot = std::filesystem::u8path(TINYENGINE_SOURCE_ROOT);
#else
        const std::filesystem::path engineRoot = std::filesystem::current_path();
#endif
        const auto result = ProjectScaffolder::Create(
            {m_newProjectName.data(), std::filesystem::u8path(m_newProjectParent.data()), engineRoot});
        if (result.success)
        {
            m_requestedProject = result.projectFile;
            m_engine.RequestExit();
        }
        else
            m_projectLauncherError = result.error;
    }

    ImGui::Dummy({0, 18});
    ImGui::SeparatorText(Localize("Open Project", "打开项目"));
    ImGui::TextUnformatted(Localize("Project File", "项目文件"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ExistingProject", m_existingProjectPath.data(), m_existingProjectPath.size());
    if (ImGui::Button(Localize("Browse...", "浏览...")))
    {
#ifdef _WIN32
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        const HWND owner = SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()), &windowInfo) == SDL_TRUE
                               ? windowInfo.info.win.window : nullptr;
        const auto selected = OpenTinyProjectDialog(owner);
        if (selected)
            std::snprintf(m_existingProjectPath.data(), m_existingProjectPath.size(), "%s", selected->u8string().c_str());
#endif
    }
    ImGui::SameLine();
    if (ImGui::Button(Localize("Open", "打开")))
    {
        const auto result = ProjectScaffolder::ValidateExisting(std::filesystem::u8path(m_existingProjectPath.data()));
        if (result.success)
        {
            m_requestedProject = result.projectFile;
            m_engine.RequestExit();
        }
        else
            m_projectLauncherError = result.error;
    }
    if (!m_projectLauncherError.empty())
    {
        ImGui::Dummy({0, 10});
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", m_projectLauncherError.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::StartSolutionGeneration()
{
    const auto projectFile = m_engine.GetProjectSettings().projectFile;
    if (projectFile.empty())
    {
        m_solutionStatus = Localize("Open a project first.", "请先打开项目。");
        return;
    }
    const auto directory = projectFile.parent_path();
    m_solutionStatus = Localize("Generating Visual Studio solution...", "正在生成 Visual Studio 解决方案...");
    m_solutionFuture = std::async(std::launch::async, [directory]() { return SolutionGenerator::Generate(directory); });
}

void EditorApp::DrawSolutionStatus()
{
    if (m_solutionFuture.valid() && m_solutionFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const auto result = m_solutionFuture.get();
        m_solutionStatus = result.success ?
            std::string(Localize("Solution generated: ", "解决方案生成完成：")) + result.solutionFile.u8string() :
            std::string(Localize("Solution generation failed:\n", "解决方案生成失败：\n")) + result.output;
        m_messages.emplace_back(m_solutionStatus);
    }
    if (m_solutionStatus.empty())
        return;
    ImGui::SetNextWindowSize({640, 180}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Localize("Visual Studio Solution###SolutionStatus", "Visual Studio 解决方案###SolutionStatus")))
    {
        ImGui::TextWrapped("%s", m_solutionStatus.c_str());
        if (!m_solutionFuture.valid())
        {
            if (ImGui::Button(Localize("Close", "关闭")))
                m_solutionStatus.clear();
        }
    }
    ImGui::End();
}

void EditorApp::HandleShortcuts()
{
    const ImGuiIO &io = ImGui::GetIO();
    // 文本、数值拖动、弹窗和视口手势拥有输入时，场景命令不得抢走按键。
    if (m_engine.IsPlaying() || io.WantTextInput || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        io.KeyAlt || io.KeySuper)
        return;
    // Escape 结束当前手势并清空选择；已产生的变更仍可通过 Undo 撤销。
    if (m_sceneCommandsFocused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        ResetSceneInteraction();
        return;
    }
    if (m_sceneOrbitActive || m_activeGizmoAxis >= 0 || m_marqueeActive)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        m_sceneMaximized = !m_sceneMaximized;
    // Shader 窗口内的 Ctrl+S 由资产文档处理，不再同时保存场景。
    if (m_shaderFocused)
        return;
    // 保存属于文档命令；对象命令只属于场景视图和层级面板。
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false))
        RequestOpenScene();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        if (io.KeyShift)
            RequestSaveCurrentSceneAs();
        else
            SaveCurrentScene();
    }
    if (io.KeyCtrl && (m_sceneCommandsFocused || m_inspectorFocused))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (io.KeyShift)
                Redo();
            else
                Undo();
        }
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Y, false))
            Redo();
    }
    if (!m_sceneCommandsFocused)
        return;
    if (io.KeyCtrl)
    {
        const auto clipboardPath = m_engine.GetProjectSettings().Resolve("Saved/EditorClipboard.prefab");
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false) && m_selected != InvalidEntity)
            Prefab::Save(*m_engine.GetContext().world, m_selected, clipboardPath);
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V, false))
        {
            // 只有确实实例化出实体才提交撤销快照；空剪贴板不得污染撤销栈或误标场景脏。
            const std::string snapshot = m_engine.GetSceneManager().CreateSnapshot();
            const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, clipboardPath);
            if (!roots.empty())
            {
                CommitUndoSnapshot(std::move(snapshot));
                m_selected = roots.front();
                m_selection = roots;
            }
        }
    }
    else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        World &world = *m_engine.GetContext().world;
        const auto selection = m_selection.empty() ? std::vector<Entity>{m_selected} : m_selection;
        if (std::any_of(selection.begin(), selection.end(), [&](Entity entity) { return world.IsAlive(entity); }))
        {
            RecordUndo();
            for (const Entity entity : selection)
                if (world.IsAlive(entity))
                    world.DestroyEntity(entity);
            ResetSceneInteraction();
        }
    }
}

void EditorApp::RequestEditorExit(const bool continueExit)
{
    // 已在处理保存/关闭事务时忽略重复的系统关闭消息。
    if (m_sceneSaveDocument != InvalidSceneDocument || m_sceneCloseDocument != InvalidSceneDocument ||
        m_shaderAction != ShaderAction::None || (!continueExit && m_exitInProgress))
        return;
    if (!continueExit)
        m_exitDiscardedDocuments.clear();
    m_exitInProgress = true;
    if (m_engine.IsPlaying())
    {
        if (!m_engine.ExitPlayMode())
        {
            m_exitInProgress = false;
            return;
        }
        ResetSceneInteraction();
    }
    for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
    {
        if (std::find(m_exitDiscardedDocuments.begin(), m_exitDiscardedDocuments.end(), index) != m_exitDiscardedDocuments.end())
            continue;
        if (!(index == m_activeSceneDocument ? m_sceneDirty : m_sceneDocuments[index].dirty))
            continue;
        if (index != m_activeSceneDocument && !SwitchSceneDocument(index))
        {
            m_exitInProgress = false;
            return;
        }
        m_pendingSelectedSceneDocument = index;
        m_requestExitPopup = true;
        return;
    }
    if (IsShaderDirty())
    {
        RequestShaderAction(ShaderAction::Exit);
        return;
    }
    // 所有确认完成后才撤掉“不保存”文档的脏标记；中途取消仍保留原来的未保存状态。
    for (const auto index : m_exitDiscardedDocuments)
    {
        m_sceneDocuments[index].dirty = false;
        if (index == m_activeSceneDocument)
            m_sceneDirty = false;
    }
    m_engine.RequestExit();
}

void EditorApp::DrawExitDialog()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({1.0f, 1.0f}, ImGuiCond_Always);
    ImGui::Begin("##ExitDialogHost", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    const char *popupName = Localize("Quit TinyEngine Editor?###EditorExitDialog", "确定退出 TinyEngine 编辑器？###EditorExitDialog");
    if (m_requestExitPopup)
    {
        ImGui::OpenPopup(popupName);
        m_requestExitPopup = false;
    }
    if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(Localize("The current scene has unsaved changes.", "当前场景存在未保存的更改。"));
    const auto &document = m_sceneDocuments[m_activeSceneDocument];
    ImGui::TextUnformatted(document.path.empty() ? Localize("Untitled", "未命名") : document.path.filename().u8string().c_str());
    ImGui::TextUnformatted(Localize("Each unsaved scene will be checked before exiting.", "退出前会逐一检查所有未保存的场景。"));
    if (ImGui::Button(Localize("Save and Continue", "保存并继续")))
    {
        if (SaveCurrentScene())
        {
            ImGui::CloseCurrentPopup();
            RequestEditorExit(true);
        }
        else if (m_sceneSaveDocument != InvalidSceneDocument)
        {
            m_exitAfterSceneSave = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Localize("Discard and Continue", "不保存并继续")))
    {
        m_exitDiscardedDocuments.push_back(m_activeSceneDocument);
        ImGui::CloseCurrentPopup();
        RequestEditorExit(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(Localize("Cancel", "取消")))
    {
        m_exitInProgress = false;
        m_exitDiscardedDocuments.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::End();
}

void EditorApp::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu(Localize("File", "文件")))
    {
        if (ImGui::MenuItem(Localize("Open Scene...", "打开场景..."), "Ctrl+O", false, !m_engine.IsPlaying()))
            RequestOpenScene();
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
        ImGui::BeginDisabled(m_solutionFuture.valid() || m_engine.GetProjectSettings().projectFile.empty());
        if (ImGui::MenuItem(Localize("Generate Visual Studio Solution", "生成 Visual Studio 解决方案")))
            StartSolutionGeneration();
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(Localize("Exit", "退出"), "Alt+F4"))
        {
            RequestEditorExit();
        }
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
        if (ImGui::Button(Localize("Open", "打开"))) RequestOpenScene();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Localize("Open scene (Ctrl+O)", "打开场景（Ctrl+O）"));
        ImGui::SameLine();
        if (ImGui::Button(m_sceneDirty ? Localize("Save *", "保存 *") : Localize("Save", "保存"))) SaveCurrentScene();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Localize("Save scene (Ctrl+S)", "保存场景（Ctrl+S）"));
        ImGui::SameLine();
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
            ResetSceneInteraction();
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

EditorApp::WorkspaceLayout EditorApp::GetWorkspaceLayout(const float width, const float height) const
{
    // 窄窗口中仍保持侧栏可读，同时为场景视图留出操作空间。
    const float leftMinimum = std::min(160.0f, width * 0.20f);
    const float rightMinimum = std::min(240.0f, width * 0.30f);
    const float sideBudget = std::max(leftMinimum + rightMinimum, width - 326.0f);
    float left = std::max(leftMinimum, width * std::clamp(m_leftPanelRatio, 0.14f, 0.28f));
    float right = std::max(rightMinimum, width * std::clamp(m_rightPanelRatio, 0.18f, 0.30f));
    const float extra = left + right - leftMinimum - rightMinimum;
    if (left + right > sideBudget && extra > 0)
    {
        const float scale = (sideBudget - leftMinimum - rightMinimum) / extra;
        left = leftMinimum + (left - leftMinimum) * scale;
        right = rightMinimum + (right - rightMinimum) * scale;
    }
    return {left, right,
            std::min(height * std::clamp(m_upperPanelRatio, 0.50f, 0.84f), height - std::min(height * 0.5f, 230.0f)),
            width * std::clamp(m_assetsPanelRatio, 0.35f, 0.75f)};
}

void EditorApp::DrawWorkspaceSplitters()
{
    const auto *viewport = ImGui::GetMainViewport();
    const auto size = viewport->WorkSize, position = viewport->WorkPos;
    const auto layout = GetWorkspaceLayout(size.x, size.y);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1, 1});
    const auto splitter = [&](const char *id, ImVec2 offset, ImVec2 extent, float &ratio, float minimum, float maximum,
                              bool horizontal, bool reverse = false) {
        ImGui::SetNextWindowPos({position.x + offset.x, position.y + offset.y});
        ImGui::SetNextWindowSize(extent);
        ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::InvisibleButton("##Grip", extent);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(70, 145, 220, 180));
        }
        if (ImGui::IsItemActive())
        {
            const auto delta = ImGui::GetIO().MouseDelta;
            if (horizontal) ratio = layout.upper / size.y;
            else if (&ratio == &m_leftPanelRatio) ratio = layout.left / size.x;
            else if (&ratio == &m_rightPanelRatio) ratio = layout.right / size.x;
            ratio = std::clamp(ratio + (reverse ? -1.0f : 1.0f) * (horizontal ? delta.y / size.y : delta.x / size.x), minimum, maximum);
        }
        if (ImGui::IsItemDeactivated()) SaveEditorSettings();
        ImGui::End();
    };
    splitter("##LeftSplit", {layout.left - 3, 0}, {6, layout.upper - 3}, m_leftPanelRatio, 0.14f, 0.28f, false);
    splitter("##RightSplit", {size.x - layout.right - 3, 0}, {6, layout.upper - 3}, m_rightPanelRatio, 0.18f, 0.30f, false, true);
    splitter("##BottomSplit", {0, layout.upper - 3}, {size.x, 6}, m_upperPanelRatio, 0.50f, 0.84f, true);
    splitter("##AssetsSplit", {layout.assets - 3, layout.upper + 3}, {6, size.y - layout.upper - 3}, m_assetsPanelRatio, 0.35f, 0.75f, false);
    ImGui::PopStyleVar(2);
}

void EditorApp::DrawHierarchy()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float top = viewport->WorkPos.y;
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float upperHeight = layout.upper;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({layout.left - 3.0f, upperHeight - 3.0f}, ImGuiCond_Always);
    ImGui::Begin(Localize("Hierarchy", "场景层级"), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_sceneCommandsFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    World &world = *m_engine.GetContext().world;
    if (!m_engine.IsPlaying() && ImGui::Button(Localize("+ Entity", "+ 实体")))
    {
        RecordUndo();
        m_selected = world.CreateEntity();
        m_selection = {m_selected};
        m_selectedAsset.clear();
    }
    std::vector<Entity> entities(world.GetEntityManager().GetAliveEntities().begin(), world.GetEntityManager().GetAliveEntities().end());
    std::sort(entities.begin(), entities.end());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##HierarchySearch", Localize("Search objects...", "搜索场景对象..."),
                             m_hierarchySearch.data(), m_hierarchySearch.size());
    const std::string search = Lowercase(m_hierarchySearch.data());
    ImGui::Separator();
    std::function<void(Entity)> drawNode = [&](const Entity entity) {
        std::vector<Entity> children;
        for (const Entity candidate : entities)
            if (const auto *relation = world.GetComponent<RelationshipComponent>(candidate); relation && relation->parent == entity)
                children.push_back(candidate);
        const auto *name = world.GetComponent<NameComponent>(entity);
        if (!search.empty() && (!name || Lowercase(name->name).find(search) == std::string::npos)) return;
        if (!search.empty()) children.clear();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (std::find(m_selection.begin(), m_selection.end(), entity) != m_selection.end() || m_selected == entity)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<std::uintptr_t>(entity)), flags, "%s",
                                            name ? name->name.c_str() : Localize("Entity", "实体"));
        if (name && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name->name.c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FocusSceneEntity(entity);
        if (ImGui::IsItemClicked())
        {
            m_selectedAsset.clear();
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
        if (!search.empty() || !relation || relation->parent == InvalidEntity || !world.IsAlive(relation->parent))
            drawNode(entity);
    }
    ImGui::End();
}

void EditorApp::DrawInspector()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float width = layout.right - 3.0f;
    const float upperHeight = layout.upper - 3.0f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - width, viewport->WorkPos.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, upperHeight}, ImGuiCond_Always);
    ImGui::Begin(Localize("Inspector", "属性检查器"), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_inspectorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                         (m_selected != InvalidEntity || m_selectedAsset.empty());
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
        {
            ImGui::TextWrapped("%s", Localize("Select an object to edit its properties.", "选择场景对象后，在这里编辑属性。"));
            ImGui::TextWrapped("%s", Localize("Click in the hierarchy or viewport.", "可点击左侧层级或场景中的对象。"));
        }
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
            std::string propertyLabel = LocalizeReflected(property.name);
            if (property.type != ReflectedPropertyType::Bool)
            {
                const float fieldWidth = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(propertyLabel.c_str()).x -
                                         ImGui::GetStyle().ItemInnerSpacing.x;
                if (fieldWidth < (property.type == ReflectedPropertyType::Vec3 ? 200.0f : 160.0f))
                {
                    ImGui::TextWrapped("%s", propertyLabel.c_str());
                    propertyLabel = "##Value";
                    ImGui::SetNextItemWidth(-1);
                }
                else
                    ImGui::SetNextItemWidth(fieldWidth);
            }
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
                    RecordPropertyUndo();
                    *static_cast<std::uint32_t *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::Entity: {
                Entity edited = value ? *static_cast<Entity *>(value) : InvalidEntity;
                changed = value && ImGui::InputScalar(propertyLabel.c_str(), ImGuiDataType_U64, &edited);
                if (changed && (edited == InvalidEntity || world.IsAlive(edited)))
                {
                    RecordPropertyUndo();
                    *static_cast<Entity *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::Float: {
                float edited = value ? *static_cast<float *>(value) : 0.0f;
                const float minimum = property.hasRange ? property.minimum : 0.0f;
                const float maximum = property.hasRange ? property.maximum : 0.0f;
                changed = value && ImGui::DragFloat(propertyLabel.c_str(), &edited, property.speed, minimum, maximum);
                if (changed)
                {
                    RecordPropertyUndo();
                    *static_cast<float *>(value) = edited;
                }
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
                if (changed)
                {
                    RecordPropertyUndo();
                    *static_cast<Vec3 *>(value) = edited;
                }
                break;
            }
            case ReflectedPropertyType::String: {
                auto *text = static_cast<std::string *>(value);
                char buffer[512]{};
                if (text)
                    std::strncpy(buffer, text->c_str(), sizeof(buffer) - 1);
                if (text && ImGui::InputText(propertyLabel.c_str(), buffer, sizeof(buffer)) && *text != buffer)
                {
                    RecordPropertyUndo();
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
                        if (accepted && *text != path)
                        {
                            RecordUndo();
                            *text = path;
                            changed = true;
                        }
                        else if (!accepted)
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
                        if (ImGui::Selectable(enumLabel.c_str(), edited == static_cast<int>(index)) && edited != static_cast<int>(index))
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
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float left = viewport->WorkPos.x + (m_sceneMaximized ? 0.0f : layout.left + 3.0f), top = viewport->WorkPos.y;
    const float width = m_sceneMaximized ? viewport->WorkSize.x : viewport->WorkSize.x - layout.left - layout.right - 6.0f;
    const float height = m_sceneMaximized ? viewport->WorkSize.y : layout.upper - 3.0f;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::Begin(Localize("Scene View", "场景视图"), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_sceneCommandsFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::BeginDisabled(m_engine.IsPlaying());
    DrawSceneTabs();
    ImGui::EndDisabled();
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
    const char *modeNames[] = {Localize("Move", "移动"), Localize("Rotate", "旋转"), Localize("Scale", "缩放")};
    const auto modeName = [&modeNames](const GizmoMode mode) {
        return mode == GizmoMode::Translate ? modeNames[0] : mode == GizmoMode::Rotate ? modeNames[1] : modeNames[2];
    };
    const auto drawModeButton = [this, &modeName](const GizmoMode mode) {
        const bool active = m_gizmoMode == mode;
        if (active)
        {
            // 当前模式用选中色持续高亮，让 1/2/3 切换有可见的状态反馈。
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        const bool clicked = ImGui::Button(mode == GizmoMode::Translate   ? Localize("1 Translate", "1 移动")
                                           : mode == GizmoMode::Rotate    ? Localize("2 Rotate", "2 旋转")
                                                                          : Localize("3 Scale", "3 缩放"));
        if (active)
            ImGui::PopStyleColor(3);
        if (clicked)
            m_gizmoMode = mode;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s",
                              mode == GizmoMode::Translate ? Localize("Move the selected entities.", "移动选中实体。")
                              : mode == GizmoMode::Rotate  ? Localize("Rotate the selected entities.", "旋转选中实体。")
                                                           : Localize("Scale the selected entities.", "缩放选中实体。"),
                              mode == GizmoMode::Translate   ? Localize("Hotkey: 1", "快捷键：1")
                              : mode == GizmoMode::Rotate    ? Localize("Hotkey: 2", "快捷键：2")
                                                             : Localize("Hotkey: 3", "快捷键：3"));
    };
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    drawModeButton(GizmoMode::Translate); ImGui::SameLine();
    drawModeButton(GizmoMode::Rotate); ImGui::SameLine();
    drawModeButton(GizmoMode::Scale);
    const bool compactToolbar = toolbarWidth < 520.0f;
    if (!compactToolbar) ImGui::SameLine();
    if (ImGui::Button(Localize("View Options...", "视图选项..."))) ImGui::OpenPopup("SceneViewOptions");
    ImGui::SameLine();
    if (ImGui::Button(m_sceneMaximized ? Localize("Restore", "还原布局") : Localize("Focus View", "放大视图")))
        m_sceneMaximized = !m_sceneMaximized;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F11");
    if (ImGui::BeginPopup("SceneViewOptions"))
    {
        ImGui::SeparatorText(Localize("Transform", "变换设置"));
        ImGui::Checkbox(Localize("Snap", "吸附"), &m_gizmoSnap);
        ImGui::SetNextItemWidth(160);
        ImGui::DragFloat(Localize("Snap step", "吸附步长"), snapValue, 0.05f, 0.001f, 100.0f);
        ImGui::BeginDisabled(m_gizmoMode == GizmoMode::Scale);
        if (ImGui::Button(m_gizmoMode == GizmoMode::Scale ? Localize("Local scale", "局部缩放") :
                          m_gizmoSpace == GizmoSpace::World ? Localize("World axes", "世界轴") : Localize("Local axes", "局部轴")))
            m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
        ImGui::EndDisabled();
        int pivot = static_cast<int>(m_pivotMode);
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo(Localize("Pivot", "轴心"), &pivot, pivotNames, 3)) m_pivotMode = static_cast<PivotMode>(pivot);
        ImGui::SeparatorText(Localize("Rendering", "画面设置"));
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat(Localize("Exposure", "曝光"), &exposure, 0.02f, 0.05f, 16.0f)) m_device->SetExposure(exposure);
        if (ImGui::Checkbox("FXAA", &fxaa)) m_device->SetFxaaEnabled(fxaa);
        if (ImGui::Checkbox(Localize("Bloom", "泛光"), &bloom)) m_device->SetBloomEnabled(bloom);
        if (ImGui::Checkbox("GTAO", &gtao)) m_device->SetGtaoEnabled(gtao);
        if (ImGui::Checkbox("TAA", &taa)) m_device->SetTaaEnabled(taa);
        if (ImGui::Button(Localize("Reload Shaders", "重载着色器"))) reloadShaders();
        ImGui::Separator();
        if (ImGui::Button(Localize("Reset layout", "恢复默认布局")))
        {
            m_leftPanelRatio = 0.18f; m_rightPanelRatio = 0.24f; m_upperPanelRatio = 0.66f; m_assetsPanelRatio = 0.62f;
            SaveEditorSettings();
        }
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("%s", Localize("WASD move | RMB look | F frame | 1/2/3 tools", "WASD 移动 | 右键转向 | F 聚焦 | 1/2/3 工具"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Localize("Click the view to move. Q/E down/up, Shift faster. Drag panel borders to resize. F11 focuses the viewport.",
                                        "点击视图后可移动。Q/E 升降，Shift 加速。拖动面板分隔线调整大小，F11 放大或还原视图。"));
    const ImVec2 available = ImGui::GetContentRegionAvail();
    // ImGui 可用区域是逻辑像素；场景渲染目标按 DPI 放大到物理像素，高分屏上视口保持清晰。
    const int targetWidth = std::max(1, static_cast<int>(available.x * m_dpiScale)),
              targetHeight = std::max(1, static_cast<int>(available.y * m_dpiScale));
    m_device->ResizeSceneRenderTarget(targetWidth, targetHeight);
    const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_device->GetSceneColorTexture())), available, {0, 1}, {1, 0});
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::SetWindowFocus();
    UpdateSceneCamera(hovered, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    m_engine.GetContext().renderer->SetCameraOverride(!m_engine.IsPlaying(), m_editorCameraPosition, m_editorCameraRotation,
                                                      m_editorCameraFov);
    const bool gizmoConsumed = !m_engine.IsPlaying() && DrawGizmo(imageMinimum.x, imageMinimum.y, available.x, available.y, hovered);
    if (!m_engine.IsPlaying() && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoConsumed)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        m_selectedAsset.clear();
        m_marqueeActive = true;
        m_marqueeStart = {mouse.x, mouse.y};
        const float normalizedX = (mouse.x - imageMinimum.x) / std::max(1.0f, available.x);
        const float normalizedY = (mouse.y - imageMinimum.y) / std::max(1.0f, available.y);
        const bool ctrl = ImGui::GetIO().KeyCtrl != 0;
        // 同一像素位置的连续点击在多个重叠命中间循环；移动到新位置或框选后重新计算命中集合。
        const auto hits = PickEntityHits(normalizedX, normalizedY);
        const bool sameSpot = m_lastPickValid && hits == m_lastPickHits && std::abs(mouse.x - m_lastPickScreen.x) < 3.0f &&
                              std::abs(mouse.y - m_lastPickScreen.y) < 3.0f;
        Entity picked = InvalidEntity;
        if (sameSpot && !m_lastPickHits.empty() && !ctrl)
        {
            m_lastPickIndex = (m_lastPickIndex + 1) % m_lastPickHits.size();
            picked = m_lastPickHits[m_lastPickIndex];
        }
        else
        {
            m_lastPickHits = hits;
            m_lastPickScreen = {mouse.x, mouse.y};
            m_lastPickValid = !m_lastPickHits.empty();
            m_lastPickIndex = 0;
            picked = m_lastPickHits.empty() ? InvalidEntity : m_lastPickHits.front();
        }
        if (ctrl && picked != InvalidEntity)
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
                // 框选产生的新选择集使下一次点击应重新计算命中，不复用旧的循环队列。
                m_lastPickValid = false;
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

// 把 96 DPI 下的逻辑像素常量换算为当前显示的实际像素，保持手写布局与字号在高分屏比例一致。
float EditorApp::Scaled(const float value) const
{
    return value * m_dpiScale;
}

// 收集射线上全部命中实体并按距离升序排序；同距时包围盒体积更小者优先，使重叠对象的循环选择稳定。
std::vector<Entity> EditorApp::PickEntityHits(const float normalizedX, const float normalizedY) const
{
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    const Vec3 right = Normalize(Cross(forward, {0.0f, 1.0f, 0.0f})), up = Normalize(Cross(right, forward));
    const float tangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f),
                aspect = std::max(0.01f, m_device->GetAspectRatio());
    const float x = normalizedX * 2.0f - 1.0f, y = 1.0f - normalizedY * 2.0f;
    const Ray ray{m_editorCameraPosition, Normalize(forward + right * (x * tangent * aspect) + up * (y * tangent))};
    World &world = *m_engine.GetContext().world;
    struct Hit
    {
        Entity entity;
        float distance;
        float volume;
    };
    std::vector<Hit> hits;
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
        if (hasBounds && IntersectRayAabb(ray, bounds, distance))
        {
            const Vec3 size = bounds.maximum - bounds.minimum;
            hits.push_back({entity, distance, size.x * size.y * size.z});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.distance < b.distance)
            return true;
        if (b.distance < a.distance)
            return false;
        return a.volume < b.volume;
    });
    std::vector<Entity> result;
    result.reserve(hits.size());
    for (const auto &hit : hits)
        result.push_back(hit.entity);
    return result;
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

void EditorApp::FocusSceneEntity(const Entity entity)
{
    World *world = m_engine.GetContext().world;
    if (m_engine.IsPlaying() || !world || !world->IsAlive(entity))
        return;
    const Mat4 matrix = world->GetWorldMatrix(entity);
    Vec3 center{matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
    float radius = 0.0f;
    Aabb bounds{};
    bool hasBounds = false;
    if (const auto *meshComponent = world->GetComponent<MeshComponent>(entity);
        meshComponent && !meshComponent->meshPath.empty() && m_engine.GetContext().resourceManager)
    {
        if (const auto mesh = m_engine.GetContext().resourceManager->Load<MeshResource>(meshComponent->meshPath))
        {
            bounds = TransformBounds(mesh->GetBounds(), matrix);
            hasBounds = true;
        }
    }
    if (!hasBounds)
    {
        if (const auto *collider = world->GetComponent<ColliderComponent>(entity))
        {
            const Vec3 extent = collider->shape == ColliderShape::Box
                                    ? collider->halfExtents
                                    : Vec3{collider->radius,
                                           collider->shape == ColliderShape::Capsule
                                               ? collider->height * 0.5f + collider->radius : collider->radius,
                                           collider->radius};
            bounds = TransformBounds({extent * -1.0f, extent}, matrix);
            hasBounds = true;
        }
    }
    if (hasBounds)
    {
        center = (bounds.minimum + bounds.maximum) * 0.5f;
        radius = Length((bounds.maximum - bounds.minimum) * 0.5f);
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float sceneWidth = std::max(1.0f, viewport->WorkSize.x - layout.left - layout.right);
    const float aspect = sceneWidth / std::max(1.0f, layout.upper);
    const float verticalTangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f);
    const float halfAngle = std::atan(verticalTangent * std::min(1.0f, aspect));
    const float distance = std::max(3.0f, radius > 0.0f ? radius * 1.25f / std::sin(halfAngle) : 3.0f);
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                        -std::cos(pitch) * std::cos(yaw)});
    m_editorCameraPosition = center - forward * distance;
}

// 多选时先求枢轴，再把同一增量应用到各实体；开始拖动时仅记录一次 Undo 快照。
void EditorApp::UpdateSceneCamera(const bool hovered, const bool focused)
{
    auto &io = ImGui::GetIO();
    const bool blocked = m_engine.IsPlaying() || io.WantTextInput || ImGui::IsAnyItemActive() || io.KeyCtrl || io.KeyAlt ||
                         io.KeySuper || m_activeGizmoAxis >= 0 || m_marqueeActive || io.AppFocusLost ||
                         ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (blocked || !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        m_sceneOrbitActive = false;
    if (blocked)
        return;
    const bool beganLook = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (beganLook)
        m_sceneOrbitActive = true;
    if (m_sceneOrbitActive && !beganLook)
    {
        // ImGui 指针坐标已是逻辑像素；捕获鼠标时忽略进入视图的首帧位移。
        m_editorCameraRotation.y += io.MouseDelta.x * 0.18f;
        m_editorCameraRotation.x = std::clamp(m_editorCameraRotation.x - io.MouseDelta.y * 0.18f, -89.0f, 89.0f);
    }
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    const Vec3 right = Normalize(Cross(forward, {0, 1, 0}));
    Vec3 cameraFocus{};
    if (const World *w = m_engine.GetContext().world; w && w->IsAlive(m_selected))
    {
        const Mat4 matrix = w->GetWorldMatrix(m_selected);
        cameraFocus = {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
    }
    const float focusDistance = std::max(0.5f, Length(cameraFocus - m_editorCameraPosition));
    if (focused || m_sceneOrbitActive)
    {
        if (!io.KeyShift)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) m_gizmoMode = GizmoMode::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) m_gizmoMode = GizmoMode::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) m_gizmoMode = GizmoMode::Scale;
        }
        Vec3 movement{};
        if (ImGui::IsKeyDown(ImGuiKey_W)) movement += forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) movement -= forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) movement += right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) movement -= right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) movement.y += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) movement.y -= 1.0f;
        if (Length(movement) > 0.0f)
            m_editorCameraPosition += Normalize(movement) * (std::max(0.5f, focusDistance * 0.6f) *
                (io.KeyShift ? 3.0f : 1.0f) * std::min(io.DeltaTime, 0.05f));
    }
    if (hovered && std::abs(io.MouseWheel) > 0.0f)
        m_editorCameraPosition += forward * (io.MouseWheel * std::max(0.05f, focusDistance * 0.12f));
}

void EditorApp::RequestOpenScene()
{
    if (m_engine.IsPlaying()) return;
    m_sceneOpenError.clear();
    m_sceneOpenFiles.clear();
    const auto root = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory);
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
        if (it->is_regular_file(error) && it->path().extension() == ".scene")
            m_sceneOpenFiles.push_back(it->path());
    std::sort(m_sceneOpenFiles.begin(), m_sceneOpenFiles.end());
    m_requestSceneOpenPopup = true;
}

void EditorApp::DrawSceneOpenDialog()
{
    ImGui::SetNextWindowSize({1, 1});
    ImGui::Begin("##SceneOpenHost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    const char *title = Localize("Open Scene###SceneOpenDialog", "打开场景###SceneOpenDialog");
    if (m_requestSceneOpenPopup) { ImGui::OpenPopup(title); m_requestSceneOpenPopup = false; }
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Localize("Choose a saved scene, or enter its path:", "选择已保存的场景，或输入场景路径："));
        ImGui::BeginChild("##SceneFiles", {560, 240}, ImGuiChildFlags_Borders);
        for (const auto &path : m_sceneOpenFiles)
        {
            const auto label = path.generic_u8string();
            if (ImGui::Selectable(label.c_str(), label == m_sceneOpenPath.data()))
            {
                std::snprintf(m_sceneOpenPath.data(), m_sceneOpenPath.size(), "%s", label.c_str());
                m_sceneOpenError.clear();
            }
        }
        if (m_sceneOpenFiles.empty()) ImGui::TextUnformatted(Localize("No project scenes found.", "项目中尚未找到场景。"));
        ImGui::EndChild();
        ImGui::SetNextItemWidth(560);
        ImGui::InputText("##SceneOpenPath", m_sceneOpenPath.data(), m_sceneOpenPath.size());
        if (!m_sceneOpenError.empty()) ImGui::TextWrapped("%s", m_sceneOpenError.c_str());
        if (ImGui::Button(Localize("Open", "打开")))
        {
            auto path = std::filesystem::u8path(m_sceneOpenPath.data());
            if (path.is_relative()) path = m_engine.GetProjectSettings().Resolve(path);
            std::error_code error;
            if (path.extension() == ".scene" && std::filesystem::is_regular_file(path, error) && OpenSceneDocument(path))
                ImGui::CloseCurrentPopup();
            else m_sceneOpenError = Localize("Unable to open scene. Check the path and scene file.", "无法打开场景，请检查路径及场景文件。");
        }
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

bool EditorApp::DrawGizmo(const float minimumX, const float minimumY, const float width, const float height, const bool sceneHovered)
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
    if (m_gizmoSpace == GizmoSpace::Local || m_gizmoMode == GizmoMode::Scale)
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
    // 仅当场景图像未被浮动窗口遮挡时才检测轴悬停与抓取开始；已在进行的拖拽继续生效，避免穿透命中。
    const float hitRadius = 7.0f * m_dpiScale, lineWidth = 2.5f * m_dpiScale, activeLineWidth = 4.0f * m_dpiScale,
                handlePixel = 5.0f * m_dpiScale;
    bool hovered = false;
    float closestDistance = hitRadius;
    int hoveredAxis = -1;
    for (int axis = 0; axis < 3; ++axis)
    {
        ImVec2 end;
        if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin + axes[axis] * handleLength, minimumX, minimumY, width,
                             height, end))
            continue;
        // 三种模式形态各异：移动显示箭头、旋转显示垂直于轴的圆环、缩放显示方块手柄，切换效果一目了然。
        const bool rotateMode = m_gizmoMode == GizmoMode::Rotate;
        const std::vector<ImVec2> ring = rotateMode
                                             ? ProjectAxisRing(m_engine.GetContext().renderer->GetFrameView(), origin, axes[axis],
                                                               handleLength, minimumX, minimumY, width, height)
                                             : std::vector<ImVec2>{};
        if (rotateMode && ring.empty())
            continue;
        const float axisDistance = rotateMode ? DistanceToPolyline(mouse, ring, true) : DistanceToSegment(mouse, start, end);
        const float projectedLength = std::hypot(end.x - start.x, end.y - start.y);
        const bool axisHovered = sceneHovered && axisDistance <= closestDistance && (rotateMode || projectedLength > 12.0f);
        if (axisHovered)
        {
            hovered = true;
            hoveredAxis = axis;
            closestDistance = axisDistance;
        }
        const ImU32 shapeColor = axisHovered || m_activeGizmoAxis == axis ? IM_COL32(255, 225, 80, 255) : colors[axis];
        if (rotateMode)
        {
            drawList->AddPolyline(ring.data(), static_cast<int>(ring.size()), shapeColor, ImDrawFlags_Closed,
                                  axisHovered ? activeLineWidth : lineWidth);
        }
        else
        {
            drawList->AddLine(start, end, shapeColor, axisHovered ? activeLineWidth : lineWidth);
            if (m_gizmoMode == GizmoMode::Translate)
            {
                const float length = std::max(0.0001f, std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y)));
                const ImVec2 direction{(end.x - start.x) / length, (end.y - start.y) / length};
                const ImVec2 perpendicular{-direction.y, direction.x};
                const float head = 11.0f * m_dpiScale, back = 4.0f * m_dpiScale, half = 4.5f * m_dpiScale;
                const ImVec2 tip{end.x + direction.x * head, end.y + direction.y * head};
                const ImVec2 base{end.x - direction.x * back, end.y - direction.y * back};
                drawList->AddTriangleFilled(tip, {base.x + perpendicular.x * half, base.y + perpendicular.y * half},
                                            {base.x - perpendicular.x * half, base.y - perpendicular.y * half}, shapeColor);
            }
            else
                drawList->AddRectFilled({end.x - handlePixel, end.y - handlePixel}, {end.x + handlePixel, end.y + handlePixel}, shapeColor,
                                        1.5f * m_dpiScale);
        }
    }
    if (sceneHovered && hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_gizmoUndoRecorded = false;
        m_activeGizmoAxis = hoveredAxis;
        m_gizmoAccumulatedMotion = 0.0f;
        m_gizmoAppliedMotion = 0.0f;
        m_gizmoDragAxis = axes[hoveredAxis];
        m_gizmoDragOrigin = origin;
        m_gizmoDragHandleLength = handleLength;
        m_gizmoDragView = m_engine.GetContext().renderer->GetFrameView();
        ImVec2 end;
        ProjectToScreen(m_gizmoDragView, origin + m_gizmoDragAxis * handleLength, minimumX, minimumY, width, height, end);
        if (m_gizmoMode == GizmoMode::Rotate)
        {
            if (!RotationPlaneVector(m_gizmoDragView, mouse, origin, m_gizmoDragAxis, minimumX, minimumY, width, height,
                                     m_gizmoRotationVector))
            {
                const Vec3 helper = std::abs(m_gizmoDragAxis.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
                const Vec3 u = Normalize(Cross(m_gizmoDragAxis, helper)), v = Normalize(Cross(m_gizmoDragAxis, u));
                float best = std::numeric_limits<float>::max();
                for (int sample = 0; sample < 128; ++sample)
                {
                    const float angle = 2 * Pi * sample / 128;
                    const Vec3 radial = u * std::cos(angle) + v * std::sin(angle);
                    ImVec2 point;
                    if (!ProjectToScreen(m_gizmoDragView, origin + radial * handleLength, minimumX, minimumY, width, height, point)) continue;
                    const float distance = std::hypot(point.x - mouse.x, point.y - mouse.y);
                    if (distance < best) { best = distance; m_gizmoRotationVector = radial; }
                }
            }
            const Vec3 radial = m_gizmoRotationVector * handleLength;
            ProjectToScreen(m_gizmoDragView, origin + radial, minimumX, minimumY, width, height, start);
            ProjectToScreen(m_gizmoDragView, origin + radial + Cross(m_gizmoDragAxis, radial) * 0.01f,
                            minimumX, minimumY, width, height, end);
            m_gizmoScreenLength = std::max(0.001f, std::hypot(end.x - start.x, end.y - start.y));
            m_gizmoScreenDirection = {(end.x - start.x) / m_gizmoScreenLength, (end.y - start.y) / m_gizmoScreenLength};
            m_gizmoScreenLength *= 100.0f;
        }
        else
        {
            m_gizmoScreenLength = std::max(1.0f, std::hypot(end.x - start.x, end.y - start.y));
            m_gizmoScreenDirection = {(end.x - start.x) / m_gizmoScreenLength, (end.y - start.y) / m_gizmoScreenLength};
        }
        // 按下这一帧的 MouseDelta 属于到达手柄的运动，不能算作拖拽增量。
        return true;
    }
    if (m_activeGizmoAxis >= 0)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_activeGizmoAxis = -1;
        else
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            const float alongAxis = delta.x * m_gizmoScreenDirection.x + delta.y * m_gizmoScreenDirection.y;
            float rawMotion = alongAxis / m_gizmoScreenLength *
                              (m_gizmoMode == GizmoMode::Translate ? m_gizmoDragHandleLength : 1.0f);
            if (m_gizmoMode == GizmoMode::Rotate)
            {
                Vec3 radial;
                if (RotationPlaneVector(m_gizmoDragView, mouse, m_gizmoDragOrigin, m_gizmoDragAxis,
                                        minimumX, minimumY, width, height, radial))
                {
                    rawMotion = std::atan2(Dot(m_gizmoDragAxis, Cross(m_gizmoRotationVector, radial)),
                                           Dot(m_gizmoRotationVector, radial)) * 180.0f / Pi;
                    m_gizmoRotationVector = radial;
                }
                else rawMotion *= 180.0f / Pi;
            }
            m_gizmoAccumulatedMotion += rawMotion;
            const float step = m_gizmoMode == GizmoMode::Translate ? m_translateSnap
                               : m_gizmoMode == GizmoMode::Rotate  ? m_rotateSnap
                                                                   : m_scaleSnap;
            const float targetMotion =
                m_gizmoSnap ? std::round(m_gizmoAccumulatedMotion / std::max(0.001f, step)) * step : m_gizmoAccumulatedMotion;
            const float motion = targetMotion - m_gizmoAppliedMotion;
            const float scaleRatio = std::max(0.001f, 1.0f + targetMotion) / std::max(0.001f, 1.0f + m_gizmoAppliedMotion);
            m_gizmoAppliedMotion = targetMotion;
            if (motion == 0.0f)
                return true;
            const auto snapshot = m_gizmoUndoRecorded ? std::string{} : m_engine.GetSceneManager().CreateSnapshot();
            // 已选子实体只随已选祖先移动一次，不再重复叠加局部位移。
            for (const Entity entity : targets)
            {
                bool inherited = false;
                const auto *relationship = world.GetComponent<RelationshipComponent>(entity);
                Entity parent = relationship ? relationship->parent : InvalidEntity;
                for (Entity ancestor = parent; world.IsAlive(ancestor); )
                {
                    if (std::find(targets.begin(), targets.end(), ancestor) != targets.end()) { inherited = true; break; }
                    const auto *r = world.GetComponent<RelationshipComponent>(ancestor);
                    ancestor = r ? r->parent : InvalidEntity;
                }
                if (inherited) continue;
                auto *transform = world.GetComponent<TransformComponent>(entity);
                if (!transform) continue;
                const Mat4 parentMatrix = world.IsAlive(parent) ? world.GetWorldMatrix(parent) : Mat4::Identity();
                Vec3 localAxis;
                if (!ToLocalVector(parentMatrix, m_gizmoDragAxis, localAxis)) continue;
                if (m_gizmoMode == GizmoMode::Translate)
                    transform->position += localAxis * motion;
                else if (m_gizmoMode == GizmoMode::Rotate)
                {
                    // 围绕当前显示的轴组合旋转，不能直接累加无关的欧拉角分量。
                    const Vec3 axis = Normalize(localAxis);
                    const float half = DegreesToRadians(motion) * 0.5f;
                    const Mat4 deltaRotation = RotationMatrix({axis.x * std::sin(half), axis.y * std::sin(half),
                                                               axis.z * std::sin(half), std::cos(half)});
                    transform->rotation = RotationEuler(deltaRotation * RotationMatrix(Quaternion::FromEulerDegrees(transform->rotation)));
                    if (m_pivotMode != PivotMode::Individual)
                    {
                        const float worldHalf = DegreesToRadians(motion) * 0.5f;
                        const Mat4 rotation = RotationMatrix({m_gizmoDragAxis.x * std::sin(worldHalf), m_gizmoDragAxis.y * std::sin(worldHalf),
                                                              m_gizmoDragAxis.z * std::sin(worldHalf), std::cos(worldHalf)});
                        const Vec3 position = TransformPoint(parentMatrix, transform->position);
                        Vec3 localPosition;
                        if (ToLocalVector(parentMatrix, m_gizmoDragOrigin + TransformPoint(rotation, position - m_gizmoDragOrigin) -
                                          MatrixColumn(parentMatrix, 3), localPosition)) transform->position = localPosition;
                    }
                }
                else
                {
                    float *component = m_activeGizmoAxis == 0 ? &transform->scale.x : m_activeGizmoAxis == 1 ? &transform->scale.y : &transform->scale.z;
                    const float oldScale = *component;
                    *component = std::max(0.001f, oldScale * scaleRatio);
                    if (m_pivotMode != PivotMode::Individual && std::abs(oldScale) > 0.0001f)
                    {
                        const Vec3 position = TransformPoint(parentMatrix, transform->position);
                        const Vec3 offset = m_gizmoDragAxis * (Dot(position - m_gizmoDragOrigin, m_gizmoDragAxis) * (*component / oldScale - 1.0f));
                        Vec3 localOffset;
                        if (ToLocalVector(parentMatrix, offset, localOffset)) transform->position += localOffset;
                    }
                }
            }
            if (!snapshot.empty() && snapshot != m_engine.GetSceneManager().CreateSnapshot())
            {
                CommitUndoSnapshot(snapshot);
                m_gizmoUndoRecorded = true;
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
        const std::string snapshot = m_engine.GetSceneManager().CreateSnapshot();
        const auto roots = Prefab::Instantiate(world, asset.path);
        if (roots.empty())
            return false;
        CommitUndoSnapshot(snapshot);
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
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float top = viewport->WorkPos.y + layout.upper + 3.0f;
    const float halfWidth = layout.assets - 3.0f;
    ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
    ImGui::Begin(Localize("Assets", "资产"), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    if (!m_engine.IsPlaying() && ImGui::Button(Localize("Create...", "新建资产..."))) ImGui::OpenPopup("CreateAssetMenu");
    if (ImGui::BeginPopup("CreateAssetMenu"))
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
        if (ImGui::MenuItem(Localize("+ Blend Tree", "+ 混合树")))
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
        if (ImGui::MenuItem(Localize("+ Animation Graph", "+ 动画图")))
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
        if (ImGui::MenuItem(Localize("+ VFX", "+ 特效")))
        {
            const auto path = createUniquePath("NewEffect", ".vfx");
            if (SaveParticleEffectAsset({}, path))
            {
                m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(path.parent_path());
                m_selectedAsset = path.generic_string();
                m_selectedAssets = {m_selectedAsset};
            }
        }
        if (ImGui::MenuItem(Localize("+ Behavior Tree", "+ 行为树")))
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
        ImGui::EndPopup();
    }
    ImGui::SameLine();
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

    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 105.0f));
    ImGui::InputTextWithHint("##AssetSearch", Localize("Search assets...", "搜索资产..."), m_assetSearch.data(), m_assetSearch.size());
    ImGui::SameLine();
    if (ImGui::Button(Localize("Filter / View", "筛选 / 显示"))) ImGui::OpenPopup("AssetViewOptions");
    if (ImGui::BeginPopup("AssetViewOptions"))
    {
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

        ImGui::EndPopup();
    }
    if (!SamePath(m_assetFolder, assetRoot) || !m_selectedAsset.empty())
    {
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

    ImGui::SameLine();
    ImGui::BeginDisabled(m_selectedAsset.empty());
    if (ImGui::SmallButton(Localize("Details / Preview", "详情 / 预览"))) ImGui::OpenPopup("AssetDetails");
    ImGui::EndDisabled();
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
        const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
        if (asset && asset->type == "Shader")
            RequestShaderAction(ShaderAction::Open, asset->path);
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
    const float browserHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("AssetBrowserItems", {0.0f, browserHeight}, ImGuiChildFlags_Borders);
    if (m_assetViewMode == AssetViewMode::List || browserHeight < m_assetThumbnailSize + 50.0f)
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
    if (ImGui::BeginPopup("AssetDetails"))
    {
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
      ImGui::EndPopup();
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

bool EditorApp::IsShaderDirty() const
{
    return !m_openShaderAsset.empty() && !m_shaderSource.empty() && m_savedShaderSource != m_shaderSource.data();
}

bool EditorApp::OpenShaderDocument(const std::string &path)
{
    if (path == m_openShaderAsset && !m_shaderEditorHidden)
        return true;
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input || input.bad())
    {
        m_shaderError = std::string(Localize("Unable to read shader: ", "无法读取着色器：")) + path;
        m_messages.push_back(m_shaderError);
        return false;
    }
    m_shaderSource.assign(std::max<std::size_t>(source.size() + 1, 65536), '\0');
    std::copy(source.begin(), source.end(), m_shaderSource.begin());
    m_savedShaderSource = source;
    m_openShaderAsset = path;
    ++m_shaderDocumentRevision;
    m_shaderEditorHidden = false;
    m_shaderError.clear();
    m_shaderCompileError.clear();
    return true;
}

bool EditorApp::SaveShaderDocument()
{
    if (m_openShaderAsset.empty() || m_shaderSource.empty())
        return false;
    const auto path = std::filesystem::u8path(m_openShaderAsset);
    // 外部编辑、重命名或删除不得被当前旧缓冲区静默覆盖。
    std::ifstream input(path, std::ios::binary);
    const std::string diskSource{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input || input.bad() || diskSource != m_savedShaderSource)
    {
        m_shaderError = Localize("The file changed or cannot be read. Keep your text, or discard and reopen the file.",
                                 "文件已被外部修改或无法读取。请保留当前文本，或放弃修改后重新打开文件。");
        return false;
    }
    input.close();
    const std::string source = m_shaderSource.data();
    if (!PlatformFile::WriteAllTextAtomic(path, source, true))
    {
        m_shaderError = Localize("Unable to save shader. Your edits are still in memory.", "着色器保存失败，编辑内容仍保留在内存中。");
        return false;
    }
    m_savedShaderSource = source;
    m_shaderError.clear();
    m_shaderCompileError.clear();
    // 磁盘保存与编译结果分开：合法写入可保存尚未编译通过的源码，运行时继续保留上一份有效 Program。
    if (m_device && (!m_device->ValidateShader(m_openShaderAsset) || !m_device->ReloadShaders()))
        m_shaderCompileError = m_device->GetLastShaderError();
    m_messages.emplace_back(std::string(Localize("Shader saved: ", "着色器已保存：")) + m_openShaderAsset);
    if (!m_shaderCompileError.empty())
        m_messages.emplace_back(std::string(Localize("Shader compilation failed: ", "着色器编译失败：")) + m_shaderCompileError);
    return true;
}

void EditorApp::RequestShaderAction(const ShaderAction action, const std::string &path)
{
    if (m_shaderAction != ShaderAction::None)
        return;
    if (action == ShaderAction::Open && path == m_openShaderAsset && !m_shaderEditorHidden)
        return;
    m_shaderAction = action;
    m_pendingShaderPath = path;
    if (IsShaderDirty())
    {
        m_shaderEditorHidden = false;
        m_requestShaderPopup = true;
    }
    else
        CompleteShaderAction();
}

void EditorApp::CompleteShaderAction()
{
    const auto action = m_shaderAction;
    const auto path = m_pendingShaderPath;
    m_shaderAction = ShaderAction::None;
    m_pendingShaderPath.clear();
    m_requestShaderPopup = false;
    if (action == ShaderAction::Open)
        OpenShaderDocument(path);
    else if (action == ShaderAction::Close)
        m_shaderEditorHidden = true;
    else if (action == ShaderAction::Exit)
        RequestEditorExit(true);
}

bool EditorApp::ResolveShaderAction(const ShaderDecision decision)
{
    if (m_shaderAction == ShaderAction::None)
        return false;
    if (decision == ShaderDecision::Cancel)
    {
        if (m_shaderAction == ShaderAction::Exit)
            m_exitInProgress = false;
        m_shaderAction = ShaderAction::None;
        m_pendingShaderPath.clear();
        m_requestShaderPopup = false;
        return true;
    }
    if (decision == ShaderDecision::Save && !SaveShaderDocument())
        return false;
    if (decision == ShaderDecision::Discard)
    {
        m_shaderSource.assign(std::max<std::size_t>(m_savedShaderSource.size() + 1, 65536), '\0');
        std::copy(m_savedShaderSource.begin(), m_savedShaderSource.end(), m_shaderSource.begin());
        ++m_shaderDocumentRevision;
        m_shaderError.clear();
    }
    CompleteShaderAction();
    return true;
}

void EditorApp::DrawShaderCloseDialog()
{
    ImGui::SetNextWindowSize({1, 1});
    ImGui::Begin("##ShaderCloseHost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    const char *title = Localize("Unsaved shader###ShaderCloseDialog", "着色器尚未保存###ShaderCloseDialog");
    if (m_requestShaderPopup)
    {
        ImGui::OpenPopup(title);
        m_requestShaderPopup = false;
    }
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Localize("Save shader changes before continuing?", "继续前是否保存着色器修改？"));
        ImGui::TextUnformatted(m_openShaderAsset.c_str());
        if (!m_shaderError.empty())
            ImGui::TextWrapped("%s", m_shaderError.c_str());
        if (ImGui::Button(Localize("Save", "保存")) && ResolveShaderAction(ShaderDecision::Save))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Discard", "放弃修改")) && ResolveShaderAction(ShaderDecision::Discard))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消")) && ResolveShaderAction(ShaderDecision::Cancel))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorApp::DrawShaderEditor()
{
    // 资产选择与已打开文档分离：点击其他资源不会丢掉 Shader 缓冲区。
    if (m_openShaderAsset.empty() || m_shaderEditorHidden)
        return;
    ImGui::SetNextWindowSize({760.0f, 520.0f}, ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin(Localize("Shader Source Editor", "着色器源码编辑器"), &open,
                      IsShaderDirty() ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None))
    {
        ImGui::End();
        if (!open)
            RequestShaderAction(ShaderAction::Close);
        return;
    }
    if (!open)
    {
        ImGui::End();
        RequestShaderAction(ShaderAction::Close);
        return;
    }
    m_shaderFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::TextUnformatted(m_openShaderAsset.c_str());
    ImGui::PushID(m_openShaderAsset.c_str());
    // 新加载或恢复的文档不能接收上一个输入控件延迟提交的文本。
    ImGui::PushID(m_shaderDocumentRevision);
    ImGui::InputTextMultiline("##ShaderSource", m_shaderSource.data(), m_shaderSource.size(), {-1.0f, -96.0f},
                             ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
                             [](ImGuiInputTextCallbackData *data) {
                                 auto &buffer = *static_cast<std::vector<char> *>(data->UserData);
                                 buffer.resize(static_cast<std::size_t>(data->BufSize));
                                 data->Buf = buffer.data();
                                 return 0;
                             }, &m_shaderSource);
    ImGui::PopID();
    ImGui::PopID();
    const auto &io = ImGui::GetIO();
    const bool saveShortcut = m_shaderFocused && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper &&
                              !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                              ImGui::IsKeyPressed(ImGuiKey_S, false);
    if (ImGui::Button(Localize("Save and Reload", "保存并重载")) || saveShortcut)
        SaveShaderDocument();
    ImGui::SameLine();
    if (IsShaderDirty())
        ImGui::TextUnformatted(Localize("Unsaved changes", "存在未保存修改"));
    else
        ImGui::TextUnformatted(Localize("Saved", "已保存"));
    if (!m_shaderError.empty())
        ImGui::TextWrapped("%s", m_shaderError.c_str());
    if (m_shaderCompileError.empty())
        ImGui::TextDisabled("%s", Localize("Compilation runs after saving.", "保存后进行编译。"));
    else
    {
        int line = 0;
        const std::string &error = m_shaderCompileError;
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
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float top = viewport->WorkPos.y + layout.upper + 3.0f;
    const float left = viewport->WorkPos.x + layout.assets + 3.0f;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({viewport->WorkPos.x + viewport->WorkSize.x - left, viewport->WorkPos.y + viewport->WorkSize.y - top},
                             ImGuiCond_Always);
    ImGui::Begin(Localize("Console / Stats", "控制台 / 统计"), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    // 保留命令输入行，只滚动输出区和可选的统计区。
    const float outputHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
    ImGui::BeginChild("ConsoleOutput", {0, outputHeight});
    const bool followOutput = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    const Renderer *renderer = m_engine.GetContext().renderer;
    if (ImGui::CollapsingHeader(Localize("Performance details", "性能详情")) &&
        ImGui::BeginTable("RendererStats", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
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
    for (const auto &message : m_messages)
        ImGui::TextWrapped("%s", message.c_str());
    if (followOutput && m_consoleRenderedMessageCount != m_messages.size())
        ImGui::SetScrollHereY(1.0f);
    m_consoleRenderedMessageCount = m_messages.size();
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##ConsoleCommand", Localize("Enter a command, then press Enter", "输入命令后按 Enter 执行"),
                               m_consoleInput.data(), m_consoleInput.size(), ImGuiInputTextFlags_EnterReturnsTrue))
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
    // 先恢复目标，再移除标签；失败时原文档和索引保持不变。
    if (closingActive && m_sceneDocuments.size() > 1)
    {
        const std::size_t next = index + 1 < m_sceneDocuments.size() ? index + 1 : index - 1;
        if (!RestoreSceneSnapshot(m_sceneDocuments[next].snapshot))
            return false;
    }
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
            m_sceneDirty = m_sceneDocuments[m_activeSceneDocument].dirty;
            m_pendingSelectedSceneDocument = m_activeSceneDocument;
        }
    }
    if (closingActive)
    {
        ResetSceneInteraction();
        m_undo = m_sceneDocuments[m_activeSceneDocument].undo;
        m_redo = m_sceneDocuments[m_activeSceneDocument].redo;
    }
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    return true;
}

bool EditorApp::SwitchSceneDocument(const std::size_t index)
{
    if (m_engine.IsPlaying() || index >= m_sceneDocuments.size() || index == m_activeSceneDocument)
        return false;
    auto &current = m_sceneDocuments[m_activeSceneDocument];
    current.snapshot = m_engine.GetSceneManager().CreateSnapshot();
    current.dirty = m_sceneDirty;
    current.undo = m_undo;
    current.redo = m_redo;
    if (!RestoreSceneSnapshot(m_sceneDocuments[index].snapshot))
        return false;
    m_activeSceneDocument = index;
    m_sceneDirty = m_sceneDocuments[index].dirty;
    ResetSceneInteraction();
    m_undo = m_sceneDocuments[index].undo;
    m_redo = m_sceneDocuments[index].redo;
    return true;
}

bool EditorApp::OpenSceneDocument(const std::filesystem::path &path)
{
    if (m_engine.IsPlaying())
        return false;
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
    current.undo = m_undo;
    current.redo = m_redo;
    if (!SceneSerializer::Load(*m_engine.GetContext().world, normalized))
    {
        m_messages.emplace_back(std::string(Localize("Failed to open scene: ", "场景打开失败：")) + normalized.generic_string());
        return false;
    }
    m_sceneDocuments.push_back({normalized, m_engine.GetSceneManager().CreateSnapshot(), false});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    m_sceneDirty = false;
    ResetSceneInteraction();
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
    current.undo = m_undo;
    current.redo = m_redo;
    m_engine.GetSceneManager().NewScene();
    m_sceneDocuments.push_back({{}, m_engine.GetSceneManager().CreateSnapshot(), true});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    m_sceneDirty = true;
    ResetSceneInteraction();
    m_undo.clear();
    m_redo.clear();
}

void EditorApp::ResetSceneInteraction()
{
    m_selected = InvalidEntity;
    m_selection.clear();
    m_lastPickValid = false;
    m_lastPickHits.clear();
    m_lastPickIndex = 0;
    m_activeGizmoAxis = -1;
    m_sceneOrbitActive = false;
    m_marqueeActive = false;
    m_inspectorEditId = 0;
}

bool EditorApp::RestoreSceneSnapshot(const std::string &snapshot)
{
    // 场景反序列化会先完成校验，再替换当前 World。
    return m_engine.GetSceneManager().RestoreSnapshot(snapshot);
}

void EditorApp::RecordPropertyUndo()
{
    // 在写入第一个实际变化之前记录；同一控件的一次连续编辑只占一条撤销记录。
    const auto id = ImGui::GetID("##PropertyUndo");
    if (m_inspectorEditId != id)
        RecordUndo();
    m_inspectorEditId = ImGui::IsItemActive() ? id : 0;
}

// 当前实现使用完整场景快照换取可靠性；记录新操作后必须清空 redo 分支。
// 写操作遵循“先快照、后变更、确认发生才提交”，避免空操作污染撤销栈或误标场景脏。
void EditorApp::CommitUndoSnapshot(std::string snapshot)
{
    if (m_engine.IsPlaying() || snapshot.empty())
        return;
    m_undo.push_back(std::move(snapshot));
    if (m_undo.size() > 64)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_sceneDirty = true;
}
void EditorApp::RecordUndo()
{
    if (m_engine.IsPlaying())
        return;
    CommitUndoSnapshot(m_engine.GetSceneManager().CreateSnapshot());
}
void EditorApp::Undo()
{
    if (m_engine.IsPlaying() || m_undo.empty())
        return;
    const auto current = m_engine.GetSceneManager().CreateSnapshot();
    if (!RestoreSceneSnapshot(m_undo.back()))
        return;
    m_redo.push_back(current);
    m_undo.pop_back();
    ResetSceneInteraction();
    m_sceneDirty = true;
}
void EditorApp::Redo()
{
    if (m_engine.IsPlaying() || m_redo.empty())
        return;
    const auto current = m_engine.GetSceneManager().CreateSnapshot();
    if (!RestoreSceneSnapshot(m_redo.back()))
        return;
    m_undo.push_back(current);
    m_redo.pop_back();
    ResetSceneInteraction();
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
        // 恢复文件由多个标签共享；保存一个标签不能丢弃其他标签的恢复数据。
        const bool hasUnsavedDocuments = std::any_of(m_sceneDocuments.begin(), m_sceneDocuments.end(),
                                                     [](const SceneDocument &open) { return open.dirty; });
        if (!hasUnsavedDocuments)
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
    bool exitNow = false;
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
        if (m_exitAfterSceneSave)
            m_exitInProgress = false;
        m_exitAfterSceneSave = false;
    };
    const auto completeSave = [this, &packageNow, &exitNow, &closeAfterSave, &resetDialog]() {
        packageNow = m_packageAfterSceneSave;
        exitNow = m_exitAfterSceneSave;
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
    if (exitNow)
        RequestEditorExit(true);
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
    const auto snapshot = m_engine.GetSceneManager().CreateSnapshot();
    const bool loaded = SceneSerializer::Load(*m_engine.GetContext().world, GetAutosavePath());
    if (loaded)
    {
        CommitUndoSnapshot(snapshot);
        ResetSceneInteraction();
    }
    m_messages.emplace_back(loaded ? Localize("Editor autosave restored", "编辑器自动保存已恢复")
                                   : Localize("Failed to restore editor autosave", "编辑器自动保存恢复失败"));
    return loaded;
}

void EditorApp::UpdateWindowTitle()
{
    if (!m_device || (m_titleInitialized && m_lastTitleDirty == m_sceneDirty))
        return;
    if (m_projectLauncherMode)
    {
        SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()),
                           Localize("TinyEngine Projects", "TinyEngine 项目"));
        m_titleInitialized = true;
        return;
    }
    const std::string title = std::string(Localize("TinyEngine Editor - ", "TinyEngine 编辑器 - ")) + m_engine.GetProjectSettings().name +
                              (m_sceneDirty ? " *" : "");
    SDL_SetWindowTitle(static_cast<SDL_Window *>(m_device->GetNativeWindow()), title.c_str());
    m_lastTitleDirty = m_sceneDirty;
    m_titleInitialized = true;
}
} // 结束 MiniEngine 命名空间
