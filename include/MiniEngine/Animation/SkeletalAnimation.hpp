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

struct Skeleton
{
    std::vector<Bone> bones;
    bool IsValid() const;
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

    bool IsValid(std::size_t boneCount) const;
    std::vector<BonePose> Sample(float time, std::size_t boneCount) const;
    std::vector<BonePose> Sample(float time, std::size_t boneCount, bool shouldLoop) const;
    bool IsCompressed() const;
    SkeletalCompressionStats Compress(const SkeletalCompressionSettings &settings = {});
    std::vector<AnimationEvent> GatherEvents(float previousTime, float currentTime, bool shouldLoop) const;
    Vec3 SampleRootMotion(float time, bool shouldLoop = true) const;
    Vec3 ExtractRootMotion(float previousTime, float currentTime, bool shouldLoop = true) const;
};

std::vector<Mat4> BuildSkinMatrices(const Skeleton &skeleton, const std::vector<BonePose> &localPose);
bool SaveSkeletonAsset(const Skeleton &skeleton, const std::filesystem::path &path);
bool LoadSkeletonAsset(const std::filesystem::path &path, Skeleton &skeleton);
bool SaveSkeletalClipAsset(const SkeletalClip &clip, const std::filesystem::path &path);
bool LoadSkeletalClipAsset(const std::filesystem::path &path, SkeletalClip &clip);

using SkeletalClipResolver = std::function<const SkeletalClip *(const std::string &)>;

struct BlendTreeSample
{
    float threshold = 0.0f;
    std::string clipPath;
};

class BlendTree1D
{
  public:
    std::string parameter = "Speed";
    std::vector<BlendTreeSample> samples;

    bool IsValid() const;
    std::vector<BonePose> Sample(float value, float time, std::size_t boneCount, const SkeletalClipResolver &resolver) const;
    std::string DominantClip(float value) const;
};

bool SaveBlendTreeAsset(const BlendTree1D &tree, const std::filesystem::path &path);
bool LoadBlendTreeAsset(const std::filesystem::path &path, BlendTree1D &tree);

struct BlendTree2DSample
{
    Vec2 position{};
    std::string clipPath;
};

class BlendTree2D
{
  public:
    std::string parameterX = "Speed";
    std::string parameterY = "Direction";
    std::vector<BlendTree2DSample> samples;

    bool IsValid() const;
    std::vector<BonePose> Sample(const Vec2 &value, float time, std::size_t boneCount, const SkeletalClipResolver &resolver) const;
    std::vector<float> CalculateWeights(const Vec2 &value) const;
};

struct RetargetBoneMap
{
    int sourceBone = -1;
    int targetBone = -1;
    float translationScale = 1.0f;
};

class SkeletonRetargeter
{
  public:
    bool BuildNameMap(const Skeleton &source, const Skeleton &target);
    void SetMap(std::vector<RetargetBoneMap> mapping);
    std::vector<BonePose> Retarget(const std::vector<BonePose> &sourcePose, std::size_t targetBoneCount) const;

  private:
    std::vector<RetargetBoneMap> m_mapping;
};

struct AnimationLayer
{
    std::vector<BonePose> pose;
    std::vector<float> boneMask;
    float weight = 1.0f;
    bool additive = false;
};

std::vector<BonePose> BlendAnimationLayers(const std::vector<BonePose> &basePose, const std::vector<AnimationLayer> &layers);
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

class AnimationGraph
{
  public:
    std::string entryState;
    std::vector<AnimationGraphParameter> parameters;
    std::vector<AnimationGraphState> states;
    std::vector<AnimationGraphTransition> transitions;

    bool IsValid() const;
    const AnimationGraphState *FindState(const std::string &name) const;
};

bool SaveAnimationGraphAsset(const AnimationGraph &graph, const std::filesystem::path &path);
bool LoadAnimationGraphAsset(const std::filesystem::path &path, AnimationGraph &graph);

class AnimationGraphPlayer
{
  public:
    using MotionSampler = std::function<std::vector<BonePose>(const std::string &, float, std::size_t)>;
    bool SetGraph(std::shared_ptr<const AnimationGraph> graph);
    void SetParameter(const std::string &name, float value);
    std::vector<BonePose> Update(float deltaTime, std::size_t boneCount, const MotionSampler &sampler);
    const std::string &GetState() const;
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
    bool AddState(std::string name, std::shared_ptr<SkeletalClip> clip);
    bool AddTransition(std::string from, std::string to, float blendSeconds, Condition condition);
    bool SetState(const std::string &name);
    std::vector<BonePose> Update(float deltaTime, std::size_t boneCount);
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
