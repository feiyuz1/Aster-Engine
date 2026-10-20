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
#include "MiniEngine/Render/MaterialCompiler.hpp"

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
std::filesystem::path FindEditorSDK()
{
    char *base = SDL_GetBasePath();
    auto sdk = ProjectScaffolder::FindSDKRoot(base ? std::filesystem::u8path(base) : std::filesystem::current_path());
    SDL_free(base);
#ifdef TINYENGINE_DEVELOPMENT_SDK_ROOT
    if (sdk.empty()) sdk = ProjectScaffolder::FindSDKRoot(std::filesystem::u8path(TINYENGINE_DEVELOPMENT_SDK_ROOT));
#endif
    return sdk;
}

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
    std::string value = normalized.generic_u8string();
#ifdef _WIN32
    value = Lowercase(std::move(value));
#endif
    return value;
}

bool SamePath(const std::filesystem::path &left, const std::filesystem::path &right)
{
    return !left.empty() && !right.empty() && ComparablePath(left) == ComparablePath(right);
}

#ifdef _WIN32
struct ProjectFolderDialogResult
{
    std::optional<std::filesystem::path> path;
    std::string error;
};

ProjectFolderDialogResult OpenProjectFolderDialog(HWND owner, const std::filesystem::path &initialDirectory, bool chinese)
{
    ProjectFolderDialogResult result;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE)
    {
        result.error=chinese ? "无法初始化目录选择窗口，请手动输入保存目录。" : "Could not initialize the folder picker. Enter the folder manually.";
        return result;
    }
    IFileOpenDialog *dialog=nullptr;
    HRESULT status=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(status) && dialog)
    {
        DWORD options=0;
        status=dialog->GetOptions(&options);
        if (SUCCEEDED(status)) status=dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        if (SUCCEEDED(status)) status=dialog->SetTitle(chinese ? L"选择项目保存目录" : L"Choose project parent folder");
        if (SUCCEEDED(status)) status=dialog->SetOkButtonLabel(chinese ? L"选择文件夹" : L"Select Folder");
        std::error_code error;
        const auto initial=initialDirectory.empty() ? std::filesystem::path{} : std::filesystem::absolute(initialDirectory,error);
        if (!error && !initial.empty() && std::filesystem::is_directory(initial,error))
        {
            IShellItem *folder=nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&folder))) && folder)
            {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(status)) status=dialog->Show(owner);
        if (SUCCEEDED(status))
        {
            IShellItem *item=nullptr;
            status=dialog->GetResult(&item);
            if (SUCCEEDED(status) && item)
            {
                PWSTR selected=nullptr;
                status=item->GetDisplayName(SIGDN_FILESYSPATH,&selected);
                if (SUCCEEDED(status) && selected)
                {
                    result.path=std::filesystem::path(selected);
                    CoTaskMemFree(selected);
                }
                item->Release();
            }
            if (!result.path) status=E_FAIL;
        }
        dialog->Release();
    }
    if (FAILED(status) && status!=HRESULT_FROM_WIN32(ERROR_CANCELLED))
        result.error=chinese ? "目录选择失败，请重试或手动输入保存目录。" : "Could not select a folder. Retry or enter the folder manually.";
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}

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
        {"MaterialGraph", "材质（节点）"},
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
        else if (line.rfind("uiScale=", 0) == 0 || line.rfind("cameraSpeed=", 0) == 0)
        {
            const bool scale = line.rfind("uiScale=", 0) == 0;
            std::istringstream value(line.substr(line.find('=') + 1));
            float number;
            if (value >> number && std::isfinite(number))
            {
                if (scale) m_uiScale = std::clamp(number, 0.85f, 1.5f);
                else m_cameraSpeed = std::clamp(number, 0.05f, 20.0f);
            }
        }
        else if (line.rfind("panels=", 0) == 0)
        {
            std::istringstream values(line.substr(7));
            bool hierarchy, inspector, assets, console, performance;
            if (values >> hierarchy >> inspector >> assets >> console >> performance)
            { m_showHierarchy=hierarchy; m_showInspector=inspector; m_showAssets=assets; m_showConsole=console; m_showPerformance=performance; }
        }
        else if (line.rfind("sceneOverlays=", 0) == 0)
        {
            std::istringstream values(line.substr(14));
            values >> m_showSceneGrid >> m_showSelectionBounds;
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
    output << "uiScale=" << m_uiScale << '\n';
    output << "cameraSpeed=" << m_cameraSpeed << '\n';
    output << "panels=" << m_showHierarchy << ' ' << m_showInspector << ' ' << m_showAssets << ' ' << m_showConsole << ' ' << m_showPerformance << '\n';
    output << "sceneOverlays=" << m_showSceneGrid << ' ' << m_showSelectionBounds << '\n';
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
#ifdef _WIN32
    // Fix DPI awareness before SDL creates any HWND. Shell context-menu extensions can otherwise
    // declare the process aware later, removing Windows' bitmap scaling halfway through a picker.
    // Keep SDL/ImGui layout in logical points while OpenGL renders the higher-resolution framebuffer.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;
    m_workspaceIniPath = m_engine.GetProjectSettings().Resolve("Saved/EditorLayout.ini").generic_u8string();
    std::error_code layoutError;
    std::filesystem::create_directories(std::filesystem::u8path(m_workspaceIniPath).parent_path(), layoutError);
    io.IniFilename = m_workspaceIniPath.c_str();
    // ImGui 逻辑坐标与 96 DPI 保持一致；高分屏清晰度由 DPI 缩放模式和字体栅格密度自动保证。
    const bool chineseFontLoaded = LoadChineseEditorFont(io);
    ApplyEditorStyle();
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
    Notify(
        Localize("Editor initialized. Edit and play states are isolated.", "编辑器初始化完成。编辑状态与运行模式相互隔离。"));
    if (!chineseFontLoaded)
        Notify(Localize("Warning: no CJK font was found. Chinese text may not display correctly.",
                                         "警告：未找到中文字体，中文界面可能无法正确显示。"), FeedbackKind::Warning);
    m_sceneDocuments.push_back({m_engine.GetSceneManager().GetCurrentScene(), m_engine.GetSceneManager().CreateSnapshot(), false});
#ifdef TINYENGINE_HAS_ASSIMP
    m_importQueue.Initialize(m_engine.GetContext().jobSystem);
#endif
    if (std::filesystem::is_regular_file(GetAutosavePath()))
        Notify(
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
    if (m_projectCreationFuture.valid())
        m_projectCreationFuture.wait();
    m_engine.GetContext().renderer->SetOverlayCallback({});
    m_device->SetEventCallback({});
    m_device->SetCloseRequestCallback({});
    m_device->EnableSceneRenderTarget(false);
    SaveEditorSettings();
    ImGui::SaveIniSettingsToDisk(m_workspaceIniPath.c_str());
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
    // SDL layout is in logical points; only framebuffer allocation uses the current monitor scale.
    m_dpiScale = std::max(0.01f, ImGui::GetIO().DisplayFramebufferScale.x);
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
    m_assetsFocused = false;
    PollAssetImport();
    DrawMenuBar();
    DrawWorkspace();
    DrawSolutionStatus();
    DrawPackagingStatus();
    ImGui::BeginDisabled(m_packageFuture.valid());
    if (!m_sceneMaximized && m_showHierarchy) DrawHierarchy();
    if (!m_sceneMaximized && m_showInspector) DrawInspector();
    DrawSceneView();
    if (!m_sceneMaximized && m_showAssets) DrawAssets();
    ImGui::EndDisabled();
    if (!m_sceneMaximized && m_showConsole) DrawConsole();
    if (!m_sceneMaximized && m_showPerformance) DrawPerformance();
    if (!m_sceneMaximized) DrawWorkspaceSplitters();
    ImGui::BeginDisabled(m_packageFuture.valid());
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
    DrawAssetPreview();
    DrawAssetReferenceViewer();
    ImGui::EndDisabled();
    DrawFeedback();
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

void EditorApp::Notify(std::string message, const FeedbackKind kind)
{
    m_messages.push_back(message);
    const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
    if (!m_feedback.empty() && m_feedback.back().message == message)
    {
        m_feedback.back().expiresAt = now + (kind == FeedbackKind::Error ? 12 : 6);
        return;
    }
    if (m_feedback.size() >= 3) m_feedback.erase(m_feedback.begin());
    m_feedback.push_back({std::move(message), kind, now + (kind == FeedbackKind::Error ? 12 : 6)});
}

void EditorApp::DrawFeedback()
{
    const double now = ImGui::GetTime();
    m_feedback.erase(std::remove_if(m_feedback.begin(), m_feedback.end(), [now](const auto &item) { return item.expiresAt <= now; }), m_feedback.end());
    const auto *viewport = ImGui::GetMainViewport();
    float bottom = viewport->WorkPos.y + viewport->WorkSize.y - 12;
    for (std::size_t i = 0; i < m_feedback.size();)
    {
        const auto &item = m_feedback[i];
        const bool error = item.kind == FeedbackKind::Error;
        const ImVec4 accent = error ? ImVec4{1,.45f,.4f,1} : item.kind == FeedbackKind::Success ? ImVec4{.4f,.8f,.55f,1} : item.kind == FeedbackKind::Warning ? ImVec4{1,.75f,.35f,1} : ImVec4{.5f,.75f,1,1};
        const auto id = "##Feedback" + std::to_string(i);
        const float width = std::min(360.0f, viewport->WorkSize.x - 24);
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 12, bottom}, ImGuiCond_Always, {1,1});
        ImGui::SetNextWindowSize({width, 0});
        ImGui::PushStyleColor(ImGuiCol_Border, accent);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(accent, "%s", error ? Localize("Error", "操作失败") : item.kind == FeedbackKind::Success ? Localize("Done", "已完成") : item.kind == FeedbackKind::Warning ? Localize("Notice", "请注意") : Localize("Status", "操作状态"));
        ImGui::SameLine(width - 36);
        const bool dismiss = ImGui::SmallButton("X");
        ImGui::TextWrapped("%s", item.message.c_str());
        bottom -= ImGui::GetWindowHeight() + 8;
        ImGui::End();
        ImGui::PopStyleVar(); ImGui::PopStyleColor();
        if (dismiss) m_feedback.erase(m_feedback.begin() + i); else ++i;
    }
}

void EditorApp::SelectAsset(const std::string &path, const bool extend)
{
    if (extend)
    {
        const auto selected = std::find(m_selectedAssets.begin(), m_selectedAssets.end(), path);
        if (selected == m_selectedAssets.end()) m_selectedAssets.push_back(path); else m_selectedAssets.erase(selected);
        m_selectedAsset = m_selectedAssets.empty() ? std::string{} : m_selectedAssets.back();
    }
    else
    {
        m_selectedAsset = path;
        m_selectedAssets = {path};
    }
    m_inspectorErrors.clear();
    m_inspectorAssetDrafts.clear();
}

void EditorApp::OpenAsset(const AssetInfo &asset)
{
    if (asset.type == "Scene")
    {
        if (m_engine.IsPlaying()) Notify(Localize("Stop play mode before opening a scene.", "停止运行后可打开场景。"), FeedbackKind::Warning);
        else OpenSceneDocument(std::filesystem::u8path(asset.path));
    }
    else if (asset.type == "Shader") RequestShaderAction(ShaderAction::Open, asset.path);
    else if (asset.type == "Material" || asset.type == "MaterialGraph" || asset.extension == ".skanim" ||
             asset.extension == ".blendtree" || asset.extension == ".animgraph" || asset.extension == ".vfx" || asset.extension == ".btgraph")
    {
        m_editingAsset = asset.path;
        if (asset.type == "MaterialGraph") m_openMaterialGraphAsset.clear();
        if (asset.type == "Material") m_openMaterialAsset.clear();
    }
    else
    {
        m_previewAsset = asset.path;
        m_showAssetPreview = true;
    }
}

void EditorApp::DrawAssetPreview()
{
    if (!m_showAssetPreview || !m_engine.GetContext().resourceManager) return;
    const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_previewAsset);
    if (!asset) { m_showAssetPreview = false; return; }
    ImGui::SetNextWindowSize({420, 400}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(Localize("Asset Preview###AssetPreview", "资产预览###AssetPreview"), &m_showAssetPreview))
    {
        ImGui::TextWrapped("%s", std::filesystem::u8path(asset->path).filename().u8string().c_str());
        ImGui::TextDisabled("%s", LocalizeReflected(asset->type).c_str());
        RequestThumbnail(*asset);
        const auto thumbnail = GetThumbnailPath(*asset);
        const auto texture = m_device && IsThumbnailReady(*asset, thumbnail) ? m_device->GetUiTexture(thumbnail.generic_u8string()) : 0;
        if (texture) ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), {192,192});
        ImGui::TextWrapped("%s", asset->path.c_str());
        ImGui::Text(Localize("Size: %llu KB", "大小：%llu KB"), static_cast<unsigned long long>((asset->byteSize + 1023) / 1024));
        if (asset->type == "Mesh" || asset->type == "Prefab")
        {
            ImGui::BeginDisabled(m_engine.IsPlaying());
            if (ImGui::Button(Localize("Place in Scene", "放入场景"))) PlaceAssetInScene(*asset);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && m_engine.IsPlaying()) ImGui::SetTooltip("%s", Localize("Stop play mode first", "请先停止运行"));
            ImGui::EndDisabled();
        }
        ImGui::SeparatorText(Localize("Dependencies", "依赖资产"));
        if (asset->dependencies.empty()) ImGui::TextDisabled("%s", Localize("No dependencies", "无依赖资产"));
        for (const auto &dependency : asset->dependencies) ImGui::TextWrapped("%s", dependency.c_str());
    }
    ImGui::End();
}

void EditorApp::StartProjectCreation()
{
    if (m_projectCreationFuture.valid() || !m_createdProject.empty()) return;
    m_creationSDKRoot = FindEditorSDK();
    const auto result = ProjectScaffolder::Create(
        {m_newProjectName.data(), std::filesystem::u8path(m_newProjectParent.data()), m_creationSDKRoot});
    m_projectLauncherError = result.error;
    if (!result.success) return;
    m_createdProject = result.projectFile;
    StartCreatedProjectBuild();
}

void EditorApp::StartCreatedProjectBuild()
{
    if (m_projectCreationFuture.valid() || m_createdProject.empty()) return;
    m_projectLauncherError.clear();
    m_projectCreationOutput.clear();
    const auto directory = m_createdProject.parent_path();
    const auto sdk = m_creationSDKRoot;
    m_projectCreationFuture = std::async(std::launch::async, [directory, sdk] {
#ifdef NDEBUG
        return SolutionGenerator::Build(directory, sdk, "Release");
#else
        return SolutionGenerator::Build(directory, sdk, "Debug");
#endif
    });
}

void EditorApp::PollProjectCreation()
{
    if (!m_projectCreationFuture.valid() ||
        m_projectCreationFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = m_projectCreationFuture.get();
    m_projectCreationOutput = result.output;
    if (result.success)
    {
        m_requestedProject = m_createdProject;
        m_engine.RequestExit();
    }
    else m_projectLauncherError = Localize(
        "Project files are ready, but code compilation failed. Fix the error below, then retry.",
        "项目文件已保留，代码编译失败。请查看下方错误，修复后点击重新编译。");
}

void EditorApp::DrawProjectLauncher()
{
    PollProjectCreation();
    const bool building = m_projectCreationFuture.valid();
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
    ImGui::BeginDisabled(building || !m_createdProject.empty());
    ImGui::TextUnformatted(Localize("Project Name", "项目名称"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ProjectName", m_newProjectName.data(), m_newProjectName.size());
    ImGui::TextUnformatted(Localize("Parent Folder", "保存到目录"));
    const float browseWidth=ImGui::CalcTextSize(Localize("Browse...", "浏览...")).x+ImGui::GetStyle().FramePadding.x*2;
    ImGui::SetNextItemWidth(std::max(1.0f,ImGui::GetContentRegionAvail().x-browseWidth-ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##ProjectParent", m_newProjectParent.data(), m_newProjectParent.size());
    ImGui::SameLine();
#ifndef _WIN32
    ImGui::BeginDisabled();
#endif
    if (ImGui::Button(Localize("Browse...##ProjectParentBrowse", "浏览...##ProjectParentBrowse")))
    {
#ifdef _WIN32
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        const HWND owner=m_device && SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()),&windowInfo)==SDL_TRUE
            ? windowInfo.info.win.window : nullptr;
        const auto selected=OpenProjectFolderDialog(owner,std::filesystem::u8path(m_newProjectParent.data()),
            m_interfaceLanguage==InterfaceLanguage::SimplifiedChinese);
        if (selected.path)
        {
            const auto path=selected.path->u8string();
            if (path.size()>=m_newProjectParent.size())
                m_projectLauncherError=Localize("The selected folder path is too long.","所选目录路径过长，请选择较短的目录。");
            else
            {
                std::snprintf(m_newProjectParent.data(),m_newProjectParent.size(),"%s",path.c_str());
                m_projectLauncherError.clear();
            }
        }
        else if (!selected.error.empty()) m_projectLauncherError=selected.error;
#endif
    }
#ifndef _WIN32
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s",Localize("Enter the parent folder path manually on this platform.","此平台请手动输入保存目录。"));
#endif
    ImGui::TextDisabled("%s", Localize("The folder must exist. The project folder is created inside it.",
                                           "此目录需已存在；项目文件夹会自动建在其中。"));
    if (ImGui::Button(Localize("Create and Open", "创建并打开")))
    {
        StartProjectCreation();
    }
    ImGui::EndDisabled();
    if (m_projectCreationFuture.valid())
    {
        ImGui::TextWrapped("%s", Localize("Preparing project code. The editor will open automatically when ready.",
            "正在准备项目代码，完成后将自动打开。首次编译可能需要稍等。"));
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-1, 0}, "");
    }
    else if (!m_createdProject.empty() && !m_projectLauncherError.empty())
    {
        ImGui::TextWrapped("%s", m_createdProject.u8string().c_str());
        if (ImGui::Button(Localize("Retry Build", "重新编译"))) StartCreatedProjectBuild();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Create Another Project", "创建其他项目")))
        {
            m_createdProject.clear();
            m_projectCreationOutput.clear();
            m_projectLauncherError.clear();
        }
    }

    ImGui::Dummy({0, 18});
    ImGui::SeparatorText(Localize("Open Project", "打开项目"));
    ImGui::BeginDisabled(m_projectCreationFuture.valid());
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
    ImGui::EndDisabled();
    if (!m_projectLauncherError.empty())
    {
        ImGui::Dummy({0, 10});
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", m_projectLauncherError.c_str());
        if (!m_projectCreationOutput.empty())
        {
            ImGui::TextUnformatted(Localize("Build details (also saved to Saved/ProjectBuild.log)",
                "编译详情（同时保存到 Saved/ProjectBuild.log）"));
            ImGui::BeginChild("ProjectBuildOutput", {0, 180}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(m_projectCreationOutput.c_str());
            ImGui::EndChild();
        }
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
    const auto sdk = FindEditorSDK();
    m_solutionFuture = std::async(std::launch::async, [directory, sdk]() { return SolutionGenerator::Generate(directory, "cmake", sdk); });
}

void EditorApp::DrawSolutionStatus()
{
    if (m_solutionFuture.valid() && m_solutionFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const auto result = m_solutionFuture.get();
        m_solutionStatus = result.success ?
            std::string(Localize("Solution generated: ", "解决方案生成完成：")) + result.solutionFile.u8string() :
            std::string(Localize("Solution generation failed:\n", "解决方案生成失败：\n")) + result.output;
        Notify(m_solutionStatus, result.success ? FeedbackKind::Success : FeedbackKind::Error);
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
    if (m_packageFuture.valid()) return;
    const ImGuiIO &io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) m_sceneOrbitActive = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_scenePanActive = false;
    if (!io.KeyAlt || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_sceneAltOrbitActive = false;
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
    if (m_viewCubeActive || m_sceneOrbitActive || m_sceneAltOrbitActive || m_scenePanActive || m_activeGizmoAxis >= 0 || m_marqueeActive)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        m_sceneMaximized = !m_sceneMaximized;
    if (m_assetsFocused && !io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        m_assetRenameRequested = true;
    // 材质文档处理自己的保存、节点删除和撤销，不得透传为场景命令。
    if (m_materialGraphEditor.IsFocused() || m_materialAssetEditor.IsFocused()) return;
    if (m_assetsFocused)
    {
        if (!io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) RequestDeleteAssets();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) RestoreDeletedAssets();
        // 资产面板的 Delete / Ctrl+Z 不属于场景对象，即便旧面板焦点尚未重置。
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))) return;
    }
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
    if (!io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false))
        FocusSceneEntity(m_selected);
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
    if (m_packageFuture.valid())
    {
        Notify(Localize("Packaging is still running. Wait for completion before exiting.", "项目正在打包，请等待完成后退出。"), FeedbackKind::Warning);
        return;
    }
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
    if (m_materialAssetEditor.IsDirty() || m_materialGraphEditor.IsDirty()) { m_requestMaterialExitPopup=true; return; }
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
    ImGui::BeginDisabled(m_packageFuture.valid());
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
        const bool canPackage = !m_engine.IsPlaying() && !m_packageFuture.valid() && !m_engine.GetProjectSettings().projectFile.empty();
        bool importBusy = false;
#ifdef TINYENGINE_HAS_ASSIMP
        importBusy = m_activeImportTask != 0;
#endif
        ImGui::BeginDisabled(!canPackage || importBusy);
        if (ImGui::MenuItem(Localize("Package Project", "打包项目")))
        {
            if (SaveCurrentScene())
                PackageProject();
            else if (m_sceneSaveDocument != InvalidSceneDocument)
            {
                m_packageAfterSceneSave = true;
                Notify(Localize("Packaging will continue after Save As succeeds.", "另存为成功后将继续打包。"));
            }
            else
                Notify(Localize("Packaging stopped because the scene could not be saved.",
                                                 "场景保存失败，已停止打包。"), FeedbackKind::Error);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (!canPackage || importBusy))
            ImGui::SetTooltip("%s", importBusy ? Localize("Wait for asset import to complete", "请先等待资产导入完成") : m_packageFuture.valid() ? Localize("Packaging is already in progress", "正在打包，请等待完成") : Localize("Stop play mode and open a project first", "请先打开项目并停止运行"));
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
    if (ImGui::BeginMenu(Localize("View", "视图")))
    {
        const auto panel = [&](const char *en, const char *zh, bool &visible) {
            if (ImGui::MenuItem(Localize(en,zh), nullptr, &visible)) SaveEditorSettings();
        };
        panel("Hierarchy", "场景层级", m_showHierarchy);
        panel("Inspector", "属性检查器", m_showInspector);
        panel("Assets", "资产", m_showAssets);
        panel("Console", "控制台", m_showConsole);
        panel("Performance", "性能", m_showPerformance);
        ImGui::Separator();
        if (ImGui::MenuItem(Localize("Focus Scene", "放大场景视图"), "F11", &m_sceneMaximized)) {}
        if (ImGui::MenuItem(Localize("Save Layout", "保存当前布局")))
            Notify(SaveWorkspace() ? Localize("Layout saved", "布局已保存") : Localize("Unable to save layout", "布局保存失败"));
        if (ImGui::MenuItem(Localize("Restore Saved Layout", "载入已保存布局")))
            Notify(RestoreWorkspace() ? Localize("Saved layout restored", "已载入保存的布局") : Localize("No saved layout is available", "尚无已保存布局"));
        if (ImGui::MenuItem(Localize("Reset Layout", "恢复默认布局"))) ResetWorkspace();
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
            if (m_engine.EnterPlayMode()) Notify(Localize("Entered play mode", "已进入运行模式"));
            else Notify(m_engine.GetProjectCodeError().empty() ? Localize("Cannot enter play mode", "无法进入运行模式") : m_engine.GetProjectCodeError(), FeedbackKind::Error);
        }
    }
    else
    {
        if (ImGui::Button(Localize("Stop", "停止")))
        {
            if (m_engine.ExitPlayMode())
            {
                ResetSceneInteraction();
                Notify(Localize("Play mode stopped; edit scene restored", "运行模式已停止，编辑场景已恢复"), FeedbackKind::Success);
            }
            else Notify(Localize("Could not restore edit scene", "恢复编辑场景失败"), FeedbackKind::Error);
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_engine.IsPlaying() ? Localize("Playing", "运行中") : Localize("Editing", "编辑中"));
    ImGui::EndDisabled();
    ImGui::EndMainMenuBar();
}

void EditorApp::PackageProject()
{
    if (m_packageFuture.valid()) return;
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask != 0)
    {
        Notify(Localize("Wait for asset import to finish before packaging.", "请等待资产导入完成后再打包。"), FeedbackKind::Warning);
        return;
    }
#endif
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
    const PackageOptions options{m_engine.GetProjectSettings().projectFile, output, gameExecutable};
    m_packageFuture = std::async(std::launch::async, [options] {
        try { return ProjectPackager::Package(options); }
        catch (const std::exception &error) { PackageResult result; result.errors.push_back(error.what()); return result; }
    });
    Notify(Localize("Packaging started. Editing is temporarily locked to keep the package consistent.", "已开始后台打包；编辑暂时锁定，以保持包内容一致。"));
}

void EditorApp::DrawPackagingStatus()
{
    if (!m_packageFuture.valid()) return;
    if (m_packageFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const auto result = m_packageFuture.get();
        if (result.success) Notify(std::string(Localize("Project packaged: ", "项目打包完成：")) + result.outputDirectory.generic_u8string(), FeedbackKind::Success);
        else for (const auto &error : result.errors) Notify(std::string(Localize("Package error: ", "打包错误：")) + error, FeedbackKind::Error);
        for (const auto &warning : result.warnings) Notify(warning, FeedbackKind::Warning);
        return;
    }
    ImGui::SetNextWindowSize({380, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin(Localize("Packaging###PackagingStatus", "项目打包###PackagingStatus"), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(Localize("Validating and packaging project assets...", "正在校验并打包项目资产..."));
    ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {340, 0}, Localize("Working", "处理中"));
    ImGui::TextWrapped("%s", Localize("You can inspect the scene. Editing unlocks when packaging completes.", "可查看场景；打包完成后自动恢复编辑，并显示结果和输出目录。"));
    ImGui::End();
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
        Notify(saved ? Localize("Interface language saved.", "界面语言已保存。")
                                      : Localize("Failed to save interface language.", "界面语言保存失败。"), saved ? FeedbackKind::Success : FeedbackKind::Error);
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderFloat(Localize("UI Scale", "界面缩放"), &m_uiScale, 0.85f, 1.5f, "%.2fx"))
    { ApplyEditorStyle(); SaveEditorSettings(); }
    ImGui::TextDisabled(
        "%s", Localize("The setting is applied immediately and restored at the next launch.", "设置立即生效，并会在下次启动时自动恢复。"));
    if (m_device)
    {
        ImGui::SeparatorText(Localize("Rendering", "渲染设置"));
        float exposure=m_device->GetExposure();
        bool fxaa=m_device->IsFxaaEnabled(), bloom=m_device->IsBloomEnabled();
        bool gtao=m_device->IsGtaoEnabled(), taa=m_device->IsTaaEnabled();
        ImGui::SetNextItemWidth(200);
        if (ImGui::DragFloat(Localize("Exposure", "曝光"), &exposure, .02f, .05f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            m_device->SetExposure(exposure);
        if (ImGui::Checkbox("FXAA", &fxaa)) m_device->SetFxaaEnabled(fxaa);
        if (ImGui::Checkbox(Localize("Bloom", "泛光"), &bloom)) m_device->SetBloomEnabled(bloom);
        if (ImGui::Checkbox("GTAO", &gtao)) m_device->SetGtaoEnabled(gtao);
        if (ImGui::Checkbox("TAA", &taa)) m_device->SetTaaEnabled(taa);
        ImGui::TextDisabled("%s",Localize("Exposure and post effects apply to Lit view.","曝光与后处理作用于光照视图。"));
        if (ImGui::Button(Localize("Reload Shaders", "重载着色器")))
        {
            if (m_device->ReloadShaders()) Notify(Localize("Shaders reloaded", "着色器已重载"));
            else Notify(std::string(Localize("Shader reload failed; kept previous version: ", "着色器重载失败，已保留上一版本："))+
                m_device->GetLastShaderError(),FeedbackKind::Error);
        }
    }
    ImGui::SeparatorText(Localize("Workspace", "工作区"));
    if (ImGui::Button(Localize("Reset layout", "恢复默认布局"))) ResetWorkspace();
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
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) return;
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
    if (!BeginEditorPanel("Hierarchy", "场景层级", m_showHierarchy, {viewport->WorkPos.x, top}, {layout.left - 3.0f, upperHeight - 3.0f}))
    { ImGui::End(); return; }
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
    if (!BeginEditorPanel("Inspector", "属性检查器", m_showInspector, {viewport->WorkPos.x + viewport->WorkSize.x - width, viewport->WorkPos.y}, {width, upperHeight}))
    { ImGui::End(); return; }
    m_inspectorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_selectedAsset.empty();
    World &world = *m_engine.GetContext().world;
    const auto inspectorSelection = GetInspectorTargets();
    if (inspectorSelection != m_inspectorSelection)
    {
        m_inspectorSelection = inspectorSelection;
        m_inspectorAssetDrafts.clear();
        m_inspectorErrors.clear();
    }
    const auto *dragPayload = ImGui::GetDragDropPayload();
    const bool draggingAsset = dragPayload && dragPayload->IsDataType("TINY_ASSET_PATH");
    if ((!m_selectedAsset.empty() && !draggingAsset) || !world.IsAlive(m_selected))
    {
        const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                   : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
        if (asset)
        {
            const auto name = std::filesystem::u8path(asset->path).filename().u8string();
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::TextDisabled("%s", LocalizeReflected(asset->type).c_str());
            ImGui::TextWrapped("%s", asset->path.c_str());
            if (ImGui::Button(Localize("Open / Preview", "打开 / 预览"), {-1, 0})) OpenAsset(*asset);
            ImGui::TextWrapped("%s", Localize("Double click to open. Drag into a compatible field or the scene to apply.", "双击打开；拖到匹配的属性字段或场景中使用。"));
            ImGui::Separator();
        }
        if (asset && asset->type == "Material")
        {
            if (auto material = m_engine.GetContext().resourceManager->Load<MaterialResource>(asset->path))
            {
                if (material->GetMaterialData().formatVersion == 2) { ImGui::End(); return; }
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
                    MaterialAssetData draft; draft.legacyData.source=output.str();
                    const auto result=m_engine.GetContext().renderer->GetMaterialSystem().ApplyInstanceDraft(asset->path,draft,[this](const auto &compiled,auto &diagnostics) { return !m_device || m_device->ValidateMaterial(compiled,diagnostics); });
                    if (result.success)
                        Notify(std::string(Localize("Material saved: ", "材质已保存：")) + asset->path, FeedbackKind::Success);
                    else
                        Notify(std::string(Localize("Failed to save material: ", "材质保存失败：")) + asset->path, FeedbackKind::Error);
                }
            }
        }
        else if (!asset)
        {
            ImGui::TextWrapped("%s", Localize("Select an object to edit its properties.", "选择场景对象后，在这里编辑属性。"));
            ImGui::TextWrapped("%s", Localize("Click in the hierarchy or viewport.", "可点击左侧层级或场景中的对象。"));
        }
        ImGui::End();
        return;
    }
    DrawEntityInspector(world);
    ImGui::End();
}

// 布局保留逻辑坐标，只应用用户的界面缩放；显示器 DPI 由 ImGui 渲染后端统一处理。
float EditorApp::Scaled(const float value) const
{
    return value * m_uiScale;
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
    float aspect = sceneWidth / std::max(1.0f, layout.upper);
    if (m_device) aspect = m_device->GetAspectRatio();
    else if (m_engine.GetContext().renderer)
    {
        const auto &projection = m_engine.GetContext().renderer->GetFrameView().projection;
        if (std::abs(projection.At(0,0)) > 0.0001f)
            aspect = std::abs(projection.At(1,1) / projection.At(0,0));
    }
    if (!std::isfinite(aspect) || aspect < 0.01f) aspect = 1.0f;
    const float verticalTangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f);
    const float halfAngle = std::atan(verticalTangent * std::min(1.0f, aspect));
    const float distance = std::max(3.0f, radius > 0.0f ? radius * 1.25f / std::sin(halfAngle) : 3.0f);
    const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
    const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                        -std::cos(pitch) * std::cos(yaw)});
    m_editorCameraPosition = center - forward * distance;
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
    if (m_packageFuture.valid()) return false;
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
    Vec3 scaleAxes[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        scaleAxes[axis] = Normalize(MatrixColumn(worldMatrix, axis));
        if (m_gizmoSpace == GizmoSpace::Local) axes[axis] = scaleAxes[axis];
    }
    const ImU32 colors[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(70, 220, 100, 255), IM_COL32(75, 135, 245, 255)};
    ImVec2 start;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_activeGizmoAxis = -1;
    if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin, minimumX, minimumY, width, height, start))
        return m_activeGizmoAxis >= 0;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    const float hitRadius = Scaled(7.0f), lineWidth = Scaled(2.5f), activeLineWidth = Scaled(4.0f),
                handlePixel = Scaled(5.0f);
    struct Handle
    {
        GizmoMode mode;
        int axis;
        Vec3 direction;
        float length;
        ImVec2 end{}, tip{};
        std::vector<ImVec2> ring;
        float distance = std::numeric_limits<float>::max();
        int priority = 0;
    };
    std::vector<Handle> handles;
    handles.reserve(9);
    const auto &view = m_engine.GetContext().renderer->GetFrameView();
    for (int axis = 0; axis < 3; ++axis)
    {
        // All three operations are present together. Distinct radial bands separate the inner cubes,
        // rotation rings and outer arrowheads; scale always follows the object's local axes.
        for (const auto mode : {GizmoMode::Rotate, GizmoMode::Translate, GizmoMode::Scale})
        {
            Handle handle{mode, axis, mode == GizmoMode::Scale ? scaleAxes[axis] : axes[axis],
                          handleLength * (mode == GizmoMode::Scale ? 0.52f : mode == GizmoMode::Rotate ? 0.8f : 1.0f)};
            if (mode == GizmoMode::Rotate)
            {
                handle.ring = ProjectAxisRing(view, origin, handle.direction, handle.length, minimumX, minimumY, width, height);
                if (handle.ring.empty()) continue;
                handle.distance = DistanceToPolyline(mouse, handle.ring, true);
                handle.priority = 1;
            }
            else
            {
                if (!ProjectToScreen(view, origin + handle.direction * handle.length, minimumX, minimumY, width, height, handle.end)) continue;
                const float projectedLength = std::hypot(handle.end.x - start.x, handle.end.y - start.y);
                // An axis facing directly into the camera has no meaningful screen drag direction.
                if (projectedLength < Scaled(mode == GizmoMode::Scale ? 8.0f : 12.0f)) continue;
                if (mode == GizmoMode::Scale)
                {
                    handle.distance = std::hypot(std::max(0.0f, std::abs(mouse.x-handle.end.x)-handlePixel),
                                                 std::max(0.0f, std::abs(mouse.y-handle.end.y)-handlePixel));
                    handle.priority = 3;
                }
                else
                {
                    const ImVec2 direction{(handle.end.x-start.x)/projectedLength, (handle.end.y-start.y)/projectedLength};
                    handle.tip = {handle.end.x + direction.x * Scaled(11.0f), handle.end.y + direction.y * Scaled(11.0f)};
                    const float headDistance = DistanceToSegment(mouse, handle.end, handle.tip);
                    handle.distance = std::min(headDistance, DistanceToSegment(mouse, start, handle.end));
                    handle.priority = headDistance <= hitRadius ? 3 : 0;
                }
            }
            handles.push_back(std::move(handle));
        }
    }
    int hoveredHandle = -1;
    float closestDistance = hitRadius;
    int priority = -1;
    if (sceneHovered && m_activeGizmoAxis < 0 && std::hypot(mouse.x-start.x, mouse.y-start.y) > hitRadius)
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            const auto &handle = handles[index];
            if (handle.distance <= hitRadius && (handle.priority > priority ||
                (handle.priority == priority && handle.distance < closestDistance)))
            { hoveredHandle = static_cast<int>(index); closestDistance = handle.distance; priority = handle.priority; }
        }
    const bool hovered = hoveredHandle >= 0;
    drawList->PushClipRect({minimumX,minimumY},{minimumX+width,minimumY+height},true);
    for (const auto mode : {GizmoMode::Rotate, GizmoMode::Translate, GizmoMode::Scale})
        for (std::size_t index=0; index<handles.size(); ++index)
        {
            const auto &handle=handles[index];
            if (handle.mode != mode) continue;
            const bool highlighted = static_cast<int>(index)==hoveredHandle ||
                (m_activeGizmoAxis==handle.axis && m_gizmoDragMode==handle.mode);
            const ImU32 color = highlighted ? IM_COL32(255,225,80,255) : colors[handle.axis];
            const float thickness = highlighted ? activeLineWidth : lineWidth;
            if (mode == GizmoMode::Rotate)
                drawList->AddPolyline(handle.ring.data(), static_cast<int>(handle.ring.size()), color, ImDrawFlags_Closed, thickness);
            else if (mode == GizmoMode::Translate)
            {
                drawList->AddLine(start,handle.end,color,thickness);
                const float length=std::hypot(handle.end.x-start.x,handle.end.y-start.y);
                const ImVec2 direction{(handle.end.x-start.x)/length,(handle.end.y-start.y)/length};
                const ImVec2 base{handle.end.x-direction.x*Scaled(4.0f),handle.end.y-direction.y*Scaled(4.0f)};
                const ImVec2 half{-direction.y*Scaled(4.5f),direction.x*Scaled(4.5f)};
                drawList->AddTriangleFilled(handle.tip,{base.x+half.x,base.y+half.y},{base.x-half.x,base.y-half.y},color);
            }
            else
            {
                const ImVec2 inner{start.x+(handle.end.x-start.x)*.35f,start.y+(handle.end.y-start.y)*.35f};
                drawList->AddLine(inner,handle.end,color,lineWidth*.6f);
                drawList->AddRectFilled({handle.end.x-handlePixel,handle.end.y-handlePixel},
                    {handle.end.x+handlePixel,handle.end.y+handlePixel},color,Scaled(1.5f));
            }
        }
    drawList->PopClipRect();
    if (hovered)
    {
        const auto &handle=handles[hoveredHandle];
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImGui::SetTooltip("%s %s",handle.mode==GizmoMode::Translate ? Localize("Move","平移") :
            handle.mode==GizmoMode::Rotate ? Localize("Rotate","旋转") : Localize("Scale (local)","缩放（局部）"),
            handle.axis==0 ? "X" : handle.axis==1 ? "Y" : "Z");
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto &handle=handles[hoveredHandle];
        m_gizmoDragMode = handle.mode;
        m_gizmoUndoRecorded = false;
        m_activeGizmoAxis = handle.axis;
        m_gizmoAccumulatedMotion = 0.0f;
        m_gizmoAppliedMotion = 0.0f;
        m_gizmoDragAxis = handle.direction;
        m_gizmoDragOrigin = origin;
        m_gizmoDragHandleLength = handle.length;
        m_gizmoDragView = m_engine.GetContext().renderer->GetFrameView();
        ImVec2 end;
        ProjectToScreen(m_gizmoDragView, origin + m_gizmoDragAxis * m_gizmoDragHandleLength, minimumX, minimumY, width, height, end);
        if (m_gizmoDragMode == GizmoMode::Rotate)
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
                    if (!ProjectToScreen(m_gizmoDragView, origin + radial * m_gizmoDragHandleLength, minimumX, minimumY, width, height, point)) continue;
                    const float distance = std::hypot(point.x - mouse.x, point.y - mouse.y);
                    if (distance < best) { best = distance; m_gizmoRotationVector = radial; }
                }
            }
            const Vec3 radial = m_gizmoRotationVector * m_gizmoDragHandleLength;
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
                              (m_gizmoDragMode == GizmoMode::Translate ? m_gizmoDragHandleLength : 1.0f);
            if (m_gizmoDragMode == GizmoMode::Rotate)
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
            const float step = m_gizmoDragMode == GizmoMode::Translate ? m_translateSnap
                               : m_gizmoDragMode == GizmoMode::Rotate  ? m_rotateSnap
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
                if (m_gizmoDragMode == GizmoMode::Translate)
                    transform->position += localAxis * motion;
                else if (m_gizmoDragMode == GizmoMode::Rotate)
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
    if (!m_engine.GetContext().jobSystem) return;
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

void EditorApp::DrawAnimationEditor()
{
    if (m_editingAsset.empty())
        return;
    const std::string extension = std::filesystem::path(m_editingAsset).extension().string();
    if (extension != ".skanim" && extension != ".blendtree" && extension != ".animgraph")
        return;
    if (m_openAnimationAsset != m_editingAsset)
    {
        m_openAnimationAsset.clear();
        bool loaded = false;
        if (extension == ".skanim")
            loaded = LoadSkeletalClipAsset(m_editingAsset, m_editSkeletalClip);
        else if (extension == ".blendtree")
            loaded = LoadBlendTreeAsset(m_editingAsset, m_editBlendTree);
        else
            loaded = LoadAnimationGraphAsset(m_editingAsset, m_editAnimationGraph);
        if (!loaded)
        {
            Notify(std::string(Localize("Unable to open animation asset: ", "无法打开动画资产：")) + m_editingAsset, FeedbackKind::Error);
            m_editingAsset.clear();
            return;
        }
        m_openAnimationAsset = m_editingAsset;
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
            Notify(std::string(Localize("Animation curve keys: ", "动画曲线关键帧：")) +
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
                Notify(std::string(Localize("Animation events saved: ", "动画事件已保存：")) + m_openAnimationAsset, FeedbackKind::Success);
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
                Notify(std::string(Localize("Blend tree saved: ", "混合树已保存：")) + m_openAnimationAsset, FeedbackKind::Success);
            }
            else
                Notify(Localize("Blend tree requires unique thresholds and valid animation clip paths.",
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
                Notify(std::string(Localize("Animation graph saved: ", "动画图已保存：")) + m_openAnimationAsset, FeedbackKind::Success);
            }
            else
                Notify(Localize("Animation graph contains an invalid state, parameter, entry, or transition.",
                                                 "动画图包含无效状态、参数、入口或过渡"));
        }
    }
    ImGui::End();
}

void EditorApp::DrawEffectEditor()
{
    if (m_editingAsset.empty() || std::filesystem::path(m_editingAsset).extension() != ".vfx")
        return;
    if (m_openEffectAsset != m_editingAsset)
    {
        if (!LoadParticleEffectAsset(m_editingAsset, m_editEffect))
        {
            Notify(std::string(Localize("Unable to open VFX asset: ", "无法打开特效资产：")) + m_editingAsset, FeedbackKind::Error);
            m_editingAsset.clear();
            return;
        }
        m_openEffectAsset = m_editingAsset;
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
            Notify(std::string(Localize("VFX saved: ", "特效已保存：")) + m_openEffectAsset, FeedbackKind::Success);
        else
            Notify(
                Localize("VFX contains an invalid capacity, lifetime, or flipbook size.", "特效包含无效的容量、生命周期或序列帧尺寸"));
    }
    ImGui::End();
}

void EditorApp::DrawBehaviorTreeEditor()
{
    if (m_editingAsset.empty() || std::filesystem::path(m_editingAsset).extension() != ".btgraph")
        return;
    if (m_openBehaviorAsset != m_editingAsset)
    {
        if (!LoadBehaviorTreeGraph(m_editingAsset, m_editBehaviorGraph))
        {
            Notify(std::string(Localize("Unable to open behavior tree: ", "无法打开行为树：")) + m_editingAsset, FeedbackKind::Error);
            m_editingAsset.clear();
            return;
        }
        m_openBehaviorAsset = m_editingAsset;
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
            Notify(std::string(Localize("Behavior tree saved: ", "行为树已保存：")) + m_openBehaviorAsset, FeedbackKind::Success);
        else
            Notify(
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
    if (m_packageFuture.valid()) return false;
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
    Notify(std::string(Localize("Shader saved: ", "着色器已保存：")) + m_openShaderAsset, FeedbackKind::Success);
    if (!m_shaderCompileError.empty())
        Notify(std::string(Localize("Shader compilation failed: ", "着色器编译失败：")) + m_shaderCompileError, FeedbackKind::Error);
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
    auto *resources=m_engine.GetContext().resourceManager; auto *renderer=m_engine.GetContext().renderer;
    if (!resources || !renderer) return;
    const bool chinese = m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese;
    m_materialGraphEditor.SetChinese(chinese); m_materialAssetEditor.SetChinese(chinese);
    const auto apply = [this](const std::string &path) { return ApplyMaterialToMeshes(path, GetInspectorTargets()); };
    const bool canApply = CanApplyMaterialToSelection();
    m_materialGraphEditor.SetApplyRequest(apply, canApply); m_materialAssetEditor.SetApplyRequest(apply, canApply);
    auto &system=renderer->GetMaterialSystem();
    const auto asset=m_editingAsset.empty() ? std::optional<AssetInfo>{} : resources->GetAssetRegistry().Find(m_editingAsset);
    if (asset && asset->type=="MaterialGraph" && m_openMaterialGraphAsset!=asset->path)
    {
        MaterialDefinitionResource definition;
        if (definition.Load(asset->path) && m_materialGraphEditor.Open(asset->path,definition.GetData())) m_openMaterialGraphAsset=asset->path;
    }
    if (asset && asset->type=="Material" && m_openMaterialAsset!=asset->path)
    {
        MaterialResource resource; auto resolved=system.Resolve(asset->path);
        if (resource.Load(asset->path) && resource.GetMaterialData().formatVersion==2 && resolved)
        {
            const auto &data=resource.GetMaterialData();
            const auto parent=system.Resolve(resources->ResolveAssetPath(data.parentPath.empty() ? data.definitionPath : data.parentPath,asset->path));
            auto schema=resolved->definition->parameters;
            if (parent) for (std::size_t i=0;i<schema.size();++i) schema[i].defaultValue=parent->values[i];
            if (m_materialAssetEditor.Open(asset->path,resource.GetMaterialData(),schema)) m_openMaterialAsset=asset->path;
        }
    }
    const auto validator=[this](const auto &compiled,auto &diagnostics) { return !m_device || m_device->ValidateMaterial(compiled,diagnostics); };
    if (!m_materialAssetEditor.GetPath().empty())
    {
        const auto &draft=m_materialAssetEditor.GetDraft();
        const auto parent=system.Resolve(resources->ResolveAssetPath(draft.parentPath.empty() ? draft.definitionPath : draft.parentPath,m_materialAssetEditor.GetPath()));
        if (parent) { auto schema=parent->definition->parameters; for (std::size_t i=0;i<schema.size();++i) schema[i].defaultValue=parent->values[i]; m_materialAssetEditor.SetSchema(std::move(schema)); }
    }
    m_materialGraphEditor.SetSaveRequest([this,&system,validator](const auto &path,const auto &data,auto &diagnostics) {
        if (m_packageFuture.valid()) return false;
        const auto result=system.ApplyDefinitionDraft(path,data,validator); diagnostics=result.diagnostics; return result.success;
    });
    m_materialAssetEditor.SetSaveRequest([this,&system,validator](const auto &path,const auto &data,auto &diagnostics) {
        if (m_packageFuture.valid()) return false;
        const auto result=system.ApplyInstanceDraft(path,data,validator); diagnostics=result.diagnostics; return result.success;
    });
    if (!m_packageFuture.valid() && asset && (asset->type=="Material" || asset->type=="MaterialGraph") && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R))
    {
        const auto result=system.Reload(asset->path,validator);
        for (const auto &d:result.diagnostics) m_messages.push_back(d.message);
        if (result.success)
        {
            const auto same=[&](const std::string &path) { return !path.empty() && resources->ResolveAssetPath(path)==resources->ResolveAssetPath(asset->path); };
            if (same(m_materialGraphEditor.GetPath()))
            { MaterialDefinitionResource loaded; if (loaded.Load(resources->ResolveAssetPath(asset->path))) m_materialGraphEditor.RefreshFromDisk(loaded.GetData()); }
            if (same(m_materialAssetEditor.GetPath()))
            { MaterialResource loaded; if (loaded.Load(resources->ResolveAssetPath(asset->path))) m_materialAssetEditor.RefreshFromDisk(loaded.GetMaterialData()); }
        }
    }
    m_materialGraphEditor.Draw(); m_materialAssetEditor.Draw();
    ImGui::Begin("##MaterialExitHost",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    const auto *exitPopup = Localize("Unsaved materials###UnsavedMaterials", "材质尚未保存###UnsavedMaterials");
    if (m_requestMaterialExitPopup) { ImGui::OpenPopup(exitPopup); m_requestMaterialExitPopup=false; }
    if (ImGui::BeginPopupModal(exitPopup,nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Localize("Save material changes before exiting?", "退出前保存材质修改？"));
        if (ImGui::Button(Localize("Save all", "全部保存")) && (!m_materialGraphEditor.IsDirty() || m_materialGraphEditor.Save()) && (!m_materialAssetEditor.IsDirty() || m_materialAssetEditor.Save())) { ImGui::CloseCurrentPopup(); RequestEditorExit(true); }
        ImGui::SameLine(); if (ImGui::Button(Localize("Discard all", "全部放弃"))) { m_materialGraphEditor.Discard(); m_materialAssetEditor.Discard(); ImGui::CloseCurrentPopup(); RequestEditorExit(true); }
        ImGui::SameLine(); if (ImGui::Button(Localize("Cancel", "取消"))) { m_exitInProgress=false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::End();
}
void EditorApp::DrawConsole()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float top = viewport->WorkPos.y + layout.upper + 3.0f;
    const float left = viewport->WorkPos.x + layout.assets + 3.0f;
    if (!BeginEditorPanel("Console / Stats", "控制台", m_showConsole, {left, top}, {viewport->WorkPos.x + viewport->WorkSize.x - left, viewport->WorkPos.y + viewport->WorkSize.y - top}))
    { ImGui::End(); return; }
    // 保留命令输入行，只滚动输出区和可选的统计区。
    const float outputHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
    ImGui::BeginChild("ConsoleOutput", {0, outputHeight});
    const bool followOutput = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    for (const auto &message : m_messages)
        ImGui::TextWrapped("%s", message.c_str());
    if (followOutput && m_consoleRenderedMessageCount != m_messages.size())
        ImGui::SetScrollHereY(1.0f);
    m_consoleRenderedMessageCount = m_messages.size();
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(m_packageFuture.valid());
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
    ImGui::EndDisabled();
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
        const std::string label = (document.path.empty() ? Localize("Untitled", "未命名") : document.path.filename().u8string()) + "##Scene" +
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
        Notify(std::string(Localize("Failed to open scene: ", "场景打开失败：")) + normalized.generic_u8string(), FeedbackKind::Error);
        return false;
    }
    m_sceneDocuments.push_back({normalized, m_engine.GetSceneManager().CreateSnapshot(), false});
    m_activeSceneDocument = m_sceneDocuments.size() - 1;
    m_pendingSelectedSceneDocument = m_activeSceneDocument;
    m_sceneDirty = false;
    ResetSceneInteraction();
    m_undo.clear();
    m_redo.clear();
    Notify(std::string(Localize("Opened scene tab: ", "已打开场景标签：")) + normalized.generic_u8string());
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
    m_viewCubeActive = false;
    m_selected = InvalidEntity;
    m_selection.clear();
    m_lastPickValid = false;
    m_lastPickHits.clear();
    m_lastPickIndex = 0;
    m_activeGizmoAxis = -1;
    m_sceneOrbitActive = false;
    m_sceneAltOrbitActive = false;
    m_scenePanActive = false;
    m_scenePreviewAsset.clear();
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
    if (m_engine.IsPlaying() || m_packageFuture.valid() || m_activeSceneDocument >= m_sceneDocuments.size())
        return false;
    const auto &document = m_sceneDocuments[m_activeSceneDocument];
    if (document.path.empty())
    {
        if (RequestSaveCurrentSceneAs())
            Notify(Localize("Choose a path before saving this untitled scene.",
                                             "未命名场景必须先选择保存路径。"));
        return false;
    }
    return SaveSceneDocumentTo(m_activeSceneDocument, document.path);
}

bool EditorApp::RequestSaveCurrentSceneAs()
{
    if (m_engine.IsPlaying() || m_packageFuture.valid() || m_activeSceneDocument >= m_sceneDocuments.size())
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
    if (m_packageFuture.valid()) return false;
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
    Notify(saved ? std::string(Localize("Scene saved: ", "场景已保存：")) + normalized.generic_u8string()
                                  : Localize("Failed to save scene", "场景保存失败"), saved ? FeedbackKind::Success : FeedbackKind::Error);
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
            Notify(Localize("Packaging canceled because Save As was canceled.", "已取消另存为，因此没有执行打包。"));
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
        const std::string name = document.path.empty() ? Localize("Untitled", "未命名") : document.path.filename().u8string();
        ImGui::TextWrapped(Localize("Save changes to '%s' before closing it?", "关闭“%s”前要保存更改吗？"), name.c_str());
        if (ImGui::Button(Localize("Save", "保存")))
        {
            const std::size_t target = m_sceneCloseDocument;
            if (target != m_activeSceneDocument && !SwitchSceneDocument(target))
                Notify(Localize("Unable to activate the scene before saving.", "保存前无法切换到该场景。"), FeedbackKind::Error);
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
        Notify(std::string(Localize("Autosave failed: ", "自动保存失败：")) + path.generic_string(), FeedbackKind::Error);
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
    Notify(loaded ? Localize("Editor autosave restored", "编辑器自动保存已恢复")
                                   : Localize("Failed to restore editor autosave", "编辑器自动保存恢复失败"), loaded ? FeedbackKind::Success : FeedbackKind::Error);
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
