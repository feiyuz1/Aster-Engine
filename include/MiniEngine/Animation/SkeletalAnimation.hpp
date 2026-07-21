// 文件：include/MiniEngine/Animation/SkeletalAnimation.hpp
// 作用：定义骨架、动画片段、压缩曲线、混合树、动画图和运行时姿势采样。
// 约定：骨骼索引必须与逆绑定矩阵及 GPU 调色板保持一致。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct Bone
{
    std::string name;
    int parent = -1;
    Mat4 inverseBind = Mat4::Identity();
};

// bones 的数组下标就是运行时统一骨骼编号，parentIndex、轨道和 GPU 矩阵调色板均引用该编号。
struct Skeleton
{
    std::vector<Bone> bones;
    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;
    /// @brief 查找 `Bone`。
    /// @param name 对象名称或注册键。
    /// @return 返回计算得到的数值、数量或索引。
    int FindBone(const std::string &name) const;
};

struct BonePose
{
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct SkeletalKeyframe
{
    float time = 0.0f;
    std::vector<BonePose> localPose;
};

struct AnimationEvent
{
    float time = 0.0f;
    std::string name;
    std::string payload;
};

struct Vec3CurveKey
{
    float time = 0.0f;
    Vec3 value{};
};

struct CompressedBoneTrack
{
    std::vector<Vec3CurveKey> positions;
    std::vector<Vec3CurveKey> rotations;
    std::vector<Vec3CurveKey> scales;
};

struct SkeletalCompressionSettings
{
    float positionTolerance = 0.001f;
    float rotationToleranceDegrees = 0.1f;
    float scaleTolerance = 0.001f;
};

struct SkeletalCompressionStats
{
    std::size_t originalCurveKeys = 0;
    std::size_t compressedCurveKeys = 0;
    float ratio = 1.0f;
};

// 骨骼动画片段。Sample 支持压缩与未压缩轨道，事件查询使用“前一时间到当前时间”的半开区间。
class SkeletalClip
{
  public:
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<SkeletalKeyframe> keyframes;
    std::vector<CompressedBoneTrack> tracks;
    std::vector<AnimationEvent> events;
    std::vector<Vec3CurveKey> rootMotion;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @param boneCount 骨架中的骨骼数量。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid(std::size_t boneCount) const;
    /// @brief 在指定位置或时间采样数据。
    /// @param time 采样或播放时间，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Sample(float time, std::size_t boneCount) const;
    /// @brief 在指定位置或时间采样数据。
    /// @param time 采样或播放时间，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @param shouldLoop 是否在到达末尾后循环。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Sample(float time, std::size_t boneCount, bool shouldLoop) const;
    /// @brief 判断是否满足 `Compressed` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsCompressed() const;
    /// @brief 按误差限制压缩动画曲线。
    /// @param settings 控制系统或操作行为的设置。
    /// @return 返回接口计算或查询得到的结果。
    SkeletalCompressionStats Compress(const SkeletalCompressionSettings &settings = {});
    /// @brief 收集指定时间范围内触发的动画事件。
    /// @param previousTime 上一帧播放时间，单位为秒。
    /// @param currentTime 时间值，单位按接口约定为秒。
    /// @param shouldLoop 是否在到达末尾后循环。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<AnimationEvent> GatherEvents(float previousTime, float currentTime, bool shouldLoop) const;
    /// @brief 采样 `RootMotion`。
    /// @param time 采样或播放时间，单位为秒。
    /// @param shouldLoop 是否在到达末尾后循环。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 SampleRootMotion(float time, bool shouldLoop = true) const;
    /// @brief 提取 `RootMotion`。
    /// @param previousTime 上一帧播放时间，单位为秒。
    /// @param currentTime 时间值，单位按接口约定为秒。
    /// @param shouldLoop 是否在到达末尾后循环。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 ExtractRootMotion(float previousTime, float currentTime, bool shouldLoop = true) const;
};

/// @brief 构建 `SkinMatrices`。
/// @param skeleton 要采样或修改的骨架。
/// @param localPose 按骨骼索引排列的局部姿势。
/// @return 返回查询或处理得到的结果集合。
std::vector<Mat4> BuildSkinMatrices(const Skeleton &skeleton, const std::vector<BonePose> &localPose);
/// @brief 保存 `SkeletonAsset`。
/// @param skeleton 要采样或修改的骨架。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SaveSkeletonAsset(const Skeleton &skeleton, const std::filesystem::path &path);
/// @brief 加载 `SkeletonAsset`。
/// @param path 文件、资源或目录路径。
/// @param skeleton 要采样或修改的骨架。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool LoadSkeletonAsset(const std::filesystem::path &path, Skeleton &skeleton);
/// @brief 保存 `SkeletalClipAsset`。
/// @param clip 要播放或采样的动画片段。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SaveSkeletalClipAsset(const SkeletalClip &clip, const std::filesystem::path &path);
/// @brief 加载 `SkeletalClipAsset`。
/// @param path 文件、资源或目录路径。
/// @param clip 要播放或采样的动画片段。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool LoadSkeletalClipAsset(const std::filesystem::path &path, SkeletalClip &clip);

using SkeletalClipResolver = std::function<const SkeletalClip *(const std::string &)>;

struct BlendTreeSample
{
    float threshold = 0.0f;
    std::string clipPath;
};

// 在相邻阈值样本间线性混合；超出范围时钳制到首尾样本，避免动画姿势外推。
class BlendTree1D
{
  public:
    std::string parameter = "Speed";
    std::vector<BlendTreeSample> samples;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;
    /// @brief 在指定位置或时间采样数据。
    /// @param value 要写入、比较或计算的值。
    /// @param time 采样或播放时间，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @param resolver 接口所需的输入参数。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Sample(float value, float time, std::size_t boneCount, const SkeletalClipResolver &resolver) const;
    /// @brief 获取当前混合权重最大的动画片段。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回对应的文本或路径。
    std::string DominantClip(float value) const;
};

/// @brief 保存 `BlendTreeAsset`。
/// @param tree 接口所需的输入参数。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SaveBlendTreeAsset(const BlendTree1D &tree, const std::filesystem::path &path);
/// @brief 加载 `BlendTreeAsset`。
/// @param path 文件、资源或目录路径。
/// @param tree 由接口读取并可能修改的输入/输出参数。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool LoadBlendTreeAsset(const std::filesystem::path &path, BlendTree1D &tree);

struct BlendTree2DSample
{
    Vec2 position{};
    std::string clipPath;
};

// 在二维参数平面内选择临近样本并归一化权重，适合速度方向等连续控制量。
class BlendTree2D
{
  public:
    std::string parameterX = "Speed";
    std::string parameterY = "Direction";
    std::vector<BlendTree2DSample> samples;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;
    /// @brief 在指定位置或时间采样数据。
    /// @param value 要写入、比较或计算的值。
    /// @param time 采样或播放时间，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @param resolver 接口所需的输入参数。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Sample(const Vec2 &value, float time, std::size_t boneCount, const SkeletalClipResolver &resolver) const;
    /// @brief 计算 `Weights`。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<float> CalculateWeights(const Vec2 &value) const;
};

struct RetargetBoneMap
{
    int sourceBone = -1;
    int targetBone = -1;
    float translationScale = 1.0f;
};

// 通过名称映射和参考姿势修正把源骨架姿势转换到目标骨架，不修改原始动画资产。
class SkeletonRetargeter
{
  public:
    /// @brief 构建 `NameMap`。
    /// @param source 源数据、源资源或源位置。
    /// @param target 目标数据、目标对象或目标位置。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool BuildNameMap(const Skeleton &source, const Skeleton &target);
    /// @brief 设置 `Map`。
    /// @param mapping 接口所需的输入参数。
    void SetMap(std::vector<RetargetBoneMap> mapping);
    /// @brief 把源骨架姿势重定向到目标骨架。
    /// @param sourcePose 按源骨架顺序排列的局部姿势。
    /// @param targetBoneCount 目标骨架中的骨骼数量。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Retarget(const std::vector<BonePose> &sourcePose, std::size_t targetBoneCount) const;

  private:
    std::vector<RetargetBoneMap> m_mapping;
};

// 分层混合通过骨骼遮罩限制影响范围；weight 为 0 的层不会参与姿势求值。
struct AnimationLayer
{
    std::vector<BonePose> pose;
    std::vector<float> boneMask;
    float weight = 1.0f;
    bool additive = false;
};

/// @brief 混合 `AnimationLayers`。
/// @param basePose 接口所需的输入参数。
/// @param layers 接口所需的输入参数。
/// @return 返回查询或处理得到的结果集合。
std::vector<BonePose> BlendAnimationLayers(const std::vector<BonePose> &basePose, const std::vector<AnimationLayer> &layers);
/// @brief 求解带极向量约束的二骨骼 IK。
/// @param skeleton 要采样或修改的骨架。
/// @param localPose 按骨骼索引排列的局部姿势。
/// @param rootBone IK 链根骨骼索引。
/// @param middleBone IK 链中间骨骼索引。
/// @param endBone IK 链末端骨骼索引。
/// @param target 目标数据、目标对象或目标位置。
/// @param pole 接口所需的输入参数。
/// @param weight 归一化或待归一化的混合权重。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SolveTwoBoneIk(const Skeleton &skeleton, std::vector<BonePose> &localPose, int rootBone, int middleBone, int endBone,
                    const Vec3 &target, const Vec3 &pole, float weight = 1.0f);

struct AnimationDebugLine
{
    Vec3 from{};
    Vec3 to{};
    int bone = -1;
};

struct AnimationDebugSnapshot
{
    std::vector<Vec3> jointPositions;
    std::vector<AnimationDebugLine> bones;
};

/// @brief 构建 `AnimationDebugSnapshot`。
/// @param skeleton 要采样或修改的骨架。
/// @param localPose 按骨骼索引排列的局部姿势。
/// @return 返回接口计算或查询得到的结果。
AnimationDebugSnapshot BuildAnimationDebugSnapshot(const Skeleton &skeleton, const std::vector<BonePose> &localPose);

struct AnimationGraphParameter
{
    std::string name;
    float defaultValue = 0.0f;
};

struct AnimationGraphState
{
    std::string name;
    std::string motionPath;
};

enum class AnimationGraphComparison
{
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual
};

struct AnimationGraphTransition
{
    std::string from;
    std::string to;
    std::string parameter;
    AnimationGraphComparison comparison = AnimationGraphComparison::Greater;
    float threshold = 0.0f;
    float blendSeconds = 0.15f;
};

// 可序列化的状态与转移描述；图本身不保存播放时间，因而可被多个 Player 共享。
class AnimationGraph
{
  public:
    std::string entryState;
    std::vector<AnimationGraphParameter> parameters;
    std::vector<AnimationGraphState> states;
    std::vector<AnimationGraphTransition> transitions;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;
    /// @brief 查找 `State`。
    /// @param name 对象名称或注册键。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const AnimationGraphState *FindState(const std::string &name) const;
};

/// @brief 保存 `AnimationGraphAsset`。
/// @param graph 要执行、保存或设置的图定义。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SaveAnimationGraphAsset(const AnimationGraph &graph, const std::filesystem::path &path);
/// @brief 加载 `AnimationGraphAsset`。
/// @param path 文件、资源或目录路径。
/// @param graph 要执行、保存或设置的图定义。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool LoadAnimationGraphAsset(const std::filesystem::path &path, AnimationGraph &graph);

// AnimationGraph 的有状态运行实例，保存当前状态、参数和转移进度。
class AnimationGraphPlayer
{
  public:
    using MotionSampler = std::function<std::vector<BonePose>(const std::string &, float, std::size_t)>;
    /// @brief 设置 `Graph`。
    /// @param graph 要执行、保存或设置的图定义。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetGraph(std::shared_ptr<const AnimationGraph> graph);
    /// @brief 设置 `Parameter`。
    /// @param name 对象名称或注册键。
    /// @param value 要写入、比较或计算的值。
    void SetParameter(const std::string &name, float value);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @param sampler 接口所需的输入参数。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Update(float deltaTime, std::size_t boneCount, const MotionSampler &sampler);
    /// @brief 获取 `State`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetState() const;
    /// @brief 获取 `Time`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetTime() const;

  private:
    std::shared_ptr<const AnimationGraph> m_graph;
    std::unordered_map<std::string, float> m_parameters;
    std::string m_state;
    std::string m_previousState;
    float m_time = 0.0f;
    float m_previousTime = 0.0f;
    float m_blendDuration = 0.0f;
    float m_blendTime = 0.0f;
};

class AnimationStateMachine
{
  public:
    using Condition = std::function<bool()>;
    /// @brief 添加 `State`。
    /// @param name 对象名称或注册键。
    /// @param clip 要播放或采样的动画片段。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool AddState(std::string name, std::shared_ptr<SkeletalClip> clip);
    /// @brief 添加 `Transition`。
    /// @param from 操作的起点、源状态或源节点。
    /// @param to 操作的终点、目标状态或目标节点。
    /// @param blendSeconds 时间值，单位按接口约定为秒。
    /// @param condition 决定转移或执行结果的条件回调。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool AddTransition(std::string from, std::string to, float blendSeconds, Condition condition);
    /// @brief 设置 `State`。
    /// @param name 对象名称或注册键。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetState(const std::string &name);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @param boneCount 骨架中的骨骼数量。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<BonePose> Update(float deltaTime, std::size_t boneCount);
    /// @brief 获取 `State`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetState() const;

  private:
    struct Transition
    {
        std::string from;
        std::string to;
        float blendSeconds = 0.0f;
        Condition condition;
    };
    std::unordered_map<std::string, std::shared_ptr<SkeletalClip>> m_states;
    std::vector<Transition> m_transitions;
    std::string m_state;
    std::string m_previousState;
    float m_time = 0.0f;
    float m_previousTime = 0.0f;
    float m_blendDuration = 0.0f;
    float m_blendTime = 0.0f;
};
} // namespace MiniEngine
