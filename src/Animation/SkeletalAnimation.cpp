// 文件：src/Animation/SkeletalAnimation.cpp
// 作用：定义骨架、动画片段、压缩曲线、混合树、动画图和运行时姿势采样。
// 约定：骨骼索引必须与逆绑定矩阵及 GPU 调色板保持一致。
// 类型：实现文件。

#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace MiniEngine
{
namespace
{
Vec3 Lerp(const Vec3 &left, const Vec3 &right, const float alpha)
{
    return left * (1.0f - alpha) + right * alpha;
}

BonePose Lerp(const BonePose &left, const BonePose &right, const float alpha)
{
    return {Lerp(left.position, right.position, alpha), Lerp(left.rotation, right.rotation, alpha), Lerp(left.scale, right.scale, alpha)};
}

float CurveError(const Vec3 &actual, const Vec3 &expected, const bool angular)
{
    const auto difference = [angular](const float left, const float right) {
        float value = std::abs(left - right);
        if (angular)
        {
            value = std::fmod(value, 360.0f);
            value = std::min(value, 360.0f - value);
        }
        return value;
    };
    return std::max({difference(actual.x, expected.x), difference(actual.y, expected.y), difference(actual.z, expected.z)});
}

// 递归保留线性插值误差最大的关键帧；终止时所有被删除点都满足给定误差上限。
void ReduceCurveRange(const std::vector<Vec3CurveKey> &source, const std::size_t first, const std::size_t last, const float tolerance,
                      const bool angular, std::vector<bool> &kept)
{
    if (last <= first + 1)
        return;
    const float span = source[last].time - source[first].time;
    float maximumError = -1.0f;
    std::size_t maximumIndex = first;
    for (std::size_t index = first + 1; index < last; ++index)
    {
        const float alpha = span > 0.0f ? (source[index].time - source[first].time) / span : 0.0f;
        const float error = CurveError(source[index].value, Lerp(source[first].value, source[last].value, alpha), angular);
        if (error > maximumError)
        {
            maximumError = error;
            maximumIndex = index;
        }
    }
    if (maximumError <= tolerance)
        return;
    kept[maximumIndex] = true;
    ReduceCurveRange(source, first, maximumIndex, tolerance, angular, kept);
    ReduceCurveRange(source, maximumIndex, last, tolerance, angular, kept);
}

std::vector<Vec3CurveKey> ReduceCurve(const std::vector<Vec3CurveKey> &source, const float tolerance, const bool angular = false)
{
    if (source.size() <= 2)
        return source;
    std::vector<bool> kept(source.size(), false);
    kept.front() = kept.back() = true;
    ReduceCurveRange(source, 0, source.size() - 1, std::max(0.0f, tolerance), angular, kept);
    std::vector<Vec3CurveKey> result;
    for (std::size_t index = 0; index < source.size(); ++index)
        if (kept[index])
            result.push_back(source[index]);
    return result;
}

bool IsCurveValid(const std::vector<Vec3CurveKey> &curve, const float duration)
{
    if (curve.empty())
        return false;
    float previous = -1.0f;
    for (const auto &key : curve)
    {
        if (key.time < previous || key.time < 0.0f || key.time > duration)
            return false;
        previous = key.time;
    }
    return true;
}

Vec3 SampleCurve(const std::vector<Vec3CurveKey> &curve, const float time)
{
    const auto upper =
        std::upper_bound(curve.begin(), curve.end(), time, [](const float value, const Vec3CurveKey &key) { return value < key.time; });
    if (upper == curve.begin())
        return curve.front().value;
    if (upper == curve.end())
        return curve.back().value;
    const auto &right = *upper;
    const auto &left = *(upper - 1);
    const float alpha = right.time > left.time ? (time - left.time) / (right.time - left.time) : 0.0f;
    return Lerp(left.value, right.value, alpha);
}

bool Compare(const float value, const AnimationGraphComparison comparison, const float threshold)
{
    constexpr float epsilon = 0.0001f;
    switch (comparison)
    {
    case AnimationGraphComparison::Less:
        return value < threshold;
    case AnimationGraphComparison::LessEqual:
        return value <= threshold;
    case AnimationGraphComparison::Greater:
        return value > threshold;
    case AnimationGraphComparison::GreaterEqual:
        return value >= threshold;
    case AnimationGraphComparison::Equal:
        return std::abs(value - threshold) <= epsilon;
    case AnimationGraphComparison::NotEqual:
        return std::abs(value - threshold) > epsilon;
    }
    return false;
}
} // namespace

bool Skeleton::IsValid() const
{
    for (std::size_t index = 0; index < bones.size(); ++index)
        if (bones[index].name.empty() || bones[index].parent >= static_cast<int>(index) || bones[index].parent < -1)
            return false;
    return !bones.empty();
}

int Skeleton::FindBone(const std::string &name) const
{
    for (std::size_t index = 0; index < bones.size(); ++index)
        if (bones[index].name == name)
            return static_cast<int>(index);
    return -1;
}

bool SkeletalClip::IsValid(const std::size_t boneCount) const
{
    if (duration <= 0.0f || boneCount == 0 || (keyframes.empty() == tracks.empty()))
        return false;
    if (!keyframes.empty())
    {
        float previous = -1.0f;
        for (const auto &keyframe : keyframes)
        {
            if (keyframe.time < previous || keyframe.time < 0.0f || keyframe.time > duration || keyframe.localPose.size() != boneCount)
                return false;
            previous = keyframe.time;
        }
    }
    if (!tracks.empty())
    {
        if (tracks.size() != boneCount)
            return false;
        for (const auto &track : tracks)
            if (!IsCurveValid(track.positions, duration) || !IsCurveValid(track.rotations, duration) ||
                !IsCurveValid(track.scales, duration))
                return false;
    }
    for (const auto &event : events)
        if (event.time < 0.0f || event.time > duration || event.name.empty())
            return false;
    if (!rootMotion.empty() && !IsCurveValid(rootMotion, duration))
        return false;
    return true;
}

std::vector<BonePose> SkeletalClip::Sample(float time, const std::size_t boneCount) const
{
    return Sample(time, boneCount, looping);
}

std::vector<BonePose> SkeletalClip::Sample(float time, const std::size_t boneCount, const bool shouldLoop) const
{
    if (!IsValid(boneCount))
        return std::vector<BonePose>(boneCount);
    if (shouldLoop)
        time = std::fmod(std::max(0.0f, time), duration);
    else
        time = std::clamp(time, 0.0f, duration);
    if (!tracks.empty())
    {
        std::vector<BonePose> result(boneCount);
        for (std::size_t index = 0; index < boneCount; ++index)
            result[index] = {SampleCurve(tracks[index].positions, time), SampleCurve(tracks[index].rotations, time),
                             SampleCurve(tracks[index].scales, time)};
        return result;
    }
    const auto upper = std::upper_bound(keyframes.begin(), keyframes.end(), time,
                                        [](const float value, const SkeletalKeyframe &key) { return value < key.time; });
    if (upper == keyframes.begin())
        return upper->localPose;
    if (upper == keyframes.end())
        return keyframes.back().localPose;
    const auto &right = *upper;
    const auto &left = *(upper - 1);
    const float alpha = right.time > left.time ? (time - left.time) / (right.time - left.time) : 0.0f;
    std::vector<BonePose> result(boneCount);
    for (std::size_t index = 0; index < boneCount; ++index)
        result[index] = Lerp(left.localPose[index], right.localPose[index], alpha);
    return result;
}

bool SkeletalClip::IsCompressed() const
{
    return !tracks.empty();
}

SkeletalCompressionStats SkeletalClip::Compress(const SkeletalCompressionSettings &settings)
{
    SkeletalCompressionStats stats;
    if (keyframes.empty() || keyframes.front().localPose.empty())
        return stats;
    const std::size_t boneCount = keyframes.front().localPose.size();
    if (!IsValid(boneCount))
        return stats;
    stats.originalCurveKeys = keyframes.size() * boneCount * 3;
    std::vector<CompressedBoneTrack> compressed(boneCount);
    for (std::size_t bone = 0; bone < boneCount; ++bone)
    {
        for (const auto &frame : keyframes)
        {
            compressed[bone].positions.push_back({frame.time, frame.localPose[bone].position});
            compressed[bone].rotations.push_back({frame.time, frame.localPose[bone].rotation});
            compressed[bone].scales.push_back({frame.time, frame.localPose[bone].scale});
        }
        compressed[bone].positions = ReduceCurve(compressed[bone].positions, settings.positionTolerance);
        compressed[bone].rotations = ReduceCurve(compressed[bone].rotations, settings.rotationToleranceDegrees, true);
        compressed[bone].scales = ReduceCurve(compressed[bone].scales, settings.scaleTolerance);
        stats.compressedCurveKeys += compressed[bone].positions.size() + compressed[bone].rotations.size() + compressed[bone].scales.size();
    }
    tracks = std::move(compressed);
    keyframes.clear();
    stats.ratio =
        stats.originalCurveKeys > 0 ? static_cast<float>(stats.compressedCurveKeys) / static_cast<float>(stats.originalCurveKeys) : 1.0f;
    return stats;
}

std::vector<AnimationEvent> SkeletalClip::GatherEvents(const float previousTime, const float currentTime, const bool shouldLoop) const
{
    std::vector<AnimationEvent> result;
    if (events.empty() || previousTime == currentTime)
        return result;
    const auto appendRange = [&](const float start, const float end, const bool forward) {
        if (forward)
        {
            for (const auto &event : events)
                if (event.time > start && event.time <= end)
                    result.push_back(event);
        }
        else
            for (auto event = events.rbegin(); event != events.rend(); ++event)
                if (event->time < start && event->time >= end)
                    result.push_back(*event);
    };
    if (!shouldLoop || duration <= 0.0f)
    {
        appendRange(std::clamp(previousTime, 0.0f, duration), std::clamp(currentTime, 0.0f, duration), currentTime > previousTime);
        return result;
    }
    const bool forward = currentTime > previousTime;
    const int firstCycle = static_cast<int>(std::floor(std::min(previousTime, currentTime) / duration));
    const int lastCycle = static_cast<int>(std::floor(std::max(previousTime, currentTime) / duration));
    const int maximumCycles = 1024;
    if (forward)
        for (int cycle = firstCycle; cycle <= lastCycle && cycle - firstCycle < maximumCycles; ++cycle)
            for (const auto &event : events)
            {
                const float occurrence = cycle * duration + event.time;
                if (occurrence > previousTime && occurrence <= currentTime)
                    result.push_back(event);
            }
    else
        for (int cycle = lastCycle; cycle >= firstCycle && lastCycle - cycle < maximumCycles; --cycle)
            for (auto event = events.rbegin(); event != events.rend(); ++event)
            {
                const float occurrence = cycle * duration + event->time;
                if (occurrence < previousTime && occurrence >= currentTime)
                    result.push_back(*event);
            }
    return result;
}

Vec3 SkeletalClip::SampleRootMotion(float time, const bool shouldLoop) const
{
    if (rootMotion.empty() || duration <= 0.0f)
        return {};
    if (shouldLoop)
        time = std::fmod(std::max(0.0f, time), duration);
    else
        time = std::clamp(time, 0.0f, duration);
    return SampleCurve(rootMotion, time);
}

// 循环跨越片段末尾时拆成两段累加，避免根位移在时间回绕处反向跳变。
Vec3 SkeletalClip::ExtractRootMotion(const float previousTime, const float currentTime, const bool shouldLoop) const
{
    if (rootMotion.empty() || duration <= 0.0f || previousTime == currentTime)
        return {};
    if (!shouldLoop)
        return SampleRootMotion(currentTime, false) - SampleRootMotion(previousTime, false);
    const Vec3 cycleDelta = rootMotion.back().value - rootMotion.front().value;
    const auto sampleUnwrapped = [&](const float time) {
        const int cycle = static_cast<int>(std::floor(time / duration));
        float local = time - static_cast<float>(cycle) * duration;
        if (local < 0.0f)
            local += duration;
        return SampleCurve(rootMotion, local) + cycleDelta * static_cast<float>(cycle);
    };
    return sampleUnwrapped(currentTime) - sampleUnwrapped(previousTime);
}

std::vector<Mat4> BuildSkinMatrices(const Skeleton &skeleton, const std::vector<BonePose> &localPose)
{
    if (!skeleton.IsValid() || localPose.size() != skeleton.bones.size())
        return {};
    std::vector<Mat4> world(skeleton.bones.size()), skin(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const auto &pose = localPose[index];
        const Mat4 local = TransformMatrix(pose.position, pose.rotation, pose.scale);
        const int parent = skeleton.bones[index].parent;
        world[index] = parent >= 0 ? world[static_cast<std::size_t>(parent)] * local : local;
        skin[index] = world[index] * skeleton.bones[index].inverseBind;
    }
    return skin;
}

bool SaveSkeletonAsset(const Skeleton &skeleton, const std::filesystem::path &path)
{
    if (!skeleton.IsValid())
        return false;
    std::ostringstream output;
    output << "TINY_SKELETON 1\n";
    for (const auto &bone : skeleton.bones)
    {
        output << "bone " << std::quoted(bone.name) << ' ' << bone.parent;
        for (const float value : bone.inverseBind.data)
            output << ' ' << value;
        output << '\n';
    }
    return PlatformFile::WriteAllText(path, output.str());
}

bool LoadSkeletonAsset(const std::filesystem::path &path, Skeleton &skeleton)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string magic, token;
    int version = 0;
    if (!(input >> magic >> version) || magic != "TINY_SKELETON" || version != 1)
        return false;
    Skeleton result;
    while (input >> token)
    {
        if (token != "bone")
            return false;
        Bone bone;
        if (!(input >> std::quoted(bone.name) >> bone.parent))
            return false;
        for (float &value : bone.inverseBind.data)
            if (!(input >> value))
                return false;
        result.bones.push_back(std::move(bone));
    }
    if (!result.IsValid())
        return false;
    skeleton = std::move(result);
    return true;
}

bool SaveSkeletalClipAsset(const SkeletalClip &clip, const std::filesystem::path &path)
{
    const std::size_t boneCount = !clip.tracks.empty()     ? clip.tracks.size()
                                  : clip.keyframes.empty() ? 0
                                                           : clip.keyframes.front().localPose.size();
    if (!clip.IsValid(boneCount))
        return false;
    std::ostringstream output;
    output << std::setprecision(9) << "TINY_SKANIM 3\nclip " << std::quoted(clip.name) << ' ' << clip.duration << ' ' << clip.looping << ' '
           << boneCount << ' ' << (clip.IsCompressed() ? "compressed" : "raw") << '\n';
    for (const auto &event : clip.events)
        output << "event " << event.time << ' ' << std::quoted(event.name) << ' ' << std::quoted(event.payload) << '\n';
    for (const auto &key : clip.rootMotion)
        output << "rootmotion " << key.time << ' ' << key.value.x << ' ' << key.value.y << ' ' << key.value.z << '\n';
    if (!clip.IsCompressed())
        for (const auto &key : clip.keyframes)
        {
            output << "key " << key.time << '\n';
            for (const auto &pose : key.localPose)
                output << "pose " << pose.position.x << ' ' << pose.position.y << ' ' << pose.position.z << ' ' << pose.rotation.x << ' '
                       << pose.rotation.y << ' ' << pose.rotation.z << ' ' << pose.scale.x << ' ' << pose.scale.y << ' ' << pose.scale.z
                       << '\n';
        }
    else
    {
        for (std::size_t index = 0; index < clip.tracks.size(); ++index)
        {
            output << "track " << index << '\n';
            const auto writeCurve = [&output](const char *token, const std::vector<Vec3CurveKey> &curve) {
                for (const auto &key : curve)
                    output << token << ' ' << key.time << ' ' << key.value.x << ' ' << key.value.y << ' ' << key.value.z << '\n';
            };
            writeCurve("position", clip.tracks[index].positions);
            writeCurve("rotation", clip.tracks[index].rotations);
            writeCurve("scale", clip.tracks[index].scales);
            output << "endtrack\n";
        }
    }
    return PlatformFile::WriteAllText(path, output.str());
}

bool LoadSkeletalClipAsset(const std::filesystem::path &path, SkeletalClip &clip)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string magic, token;
    int version = 0;
    std::size_t boneCount = 0;
    SkeletalClip result;
    if (!(input >> magic >> version >> token >> std::quoted(result.name) >> result.duration >> result.looping >> boneCount) ||
        magic != "TINY_SKANIM" || (version < 1 || version > 3) || token != "clip" || boneCount > 256)
        return false;
    std::string storage = "raw";
    if (version >= 2 && !(input >> storage))
        return false;
    if (storage == "compressed")
        result.tracks.resize(boneCount);
    else if (storage != "raw")
        return false;
    while (input >> token)
    {
        if (token == "event" && version >= 2)
        {
            AnimationEvent event;
            if (!(input >> event.time >> std::quoted(event.name) >> std::quoted(event.payload)))
                return false;
            result.events.push_back(std::move(event));
        }
        else if (token == "rootmotion" && version >= 3)
        {
            Vec3CurveKey key;
            if (!(input >> key.time >> key.value.x >> key.value.y >> key.value.z))
                return false;
            result.rootMotion.push_back(key);
        }
        else if (token == "key" && storage == "raw")
        {
            SkeletalKeyframe key;
            input >> key.time;
            key.localPose.resize(boneCount);
            for (auto &pose : key.localPose)
                if (!(input >> token >> pose.position.x >> pose.position.y >> pose.position.z >> pose.rotation.x >> pose.rotation.y >>
                      pose.rotation.z >> pose.scale.x >> pose.scale.y >> pose.scale.z) ||
                    token != "pose")
                    return false;
            result.keyframes.push_back(std::move(key));
        }
        else if (token == "track" && storage == "compressed")
        {
            std::size_t trackIndex = 0;
            if (!(input >> trackIndex) || trackIndex >= result.tracks.size())
                return false;
            auto &track = result.tracks[trackIndex];
            while (input >> token && token != "endtrack")
            {
                Vec3CurveKey key;
                if (!(input >> key.time >> key.value.x >> key.value.y >> key.value.z))
                    return false;
                if (token == "position")
                    track.positions.push_back(key);
                else if (token == "rotation")
                    track.rotations.push_back(key);
                else if (token == "scale")
                    track.scales.push_back(key);
                else
                    return false;
            }
            if (token != "endtrack")
                return false;
        }
        else
            return false;
    }
    std::sort(result.events.begin(), result.events.end(),
              [](const AnimationEvent &left, const AnimationEvent &right) { return left.time < right.time; });
    if (!result.rootMotion.empty() && !IsCurveValid(result.rootMotion, result.duration))
        return false;
    if (!result.IsValid(boneCount))
        return false;
    clip = std::move(result);
    return true;
}

bool BlendTree1D::IsValid() const
{
    if (parameter.empty() || samples.empty())
        return false;
    float previous = -std::numeric_limits<float>::infinity();
    for (const auto &sample : samples)
    {
        if (sample.clipPath.empty() || sample.threshold <= previous)
            return false;
        previous = sample.threshold;
    }
    return true;
}

std::vector<BonePose> BlendTree1D::Sample(const float value, const float time, const std::size_t boneCount,
                                          const SkeletalClipResolver &resolver) const
{
    if (!IsValid() || !resolver)
        return std::vector<BonePose>(boneCount);
    const auto upper = std::upper_bound(samples.begin(), samples.end(), value,
                                        [](const float threshold, const BlendTreeSample &sample) { return threshold < sample.threshold; });
    const BlendTreeSample &right = upper == samples.end() ? samples.back() : *upper;
    const BlendTreeSample &left = upper == samples.begin() ? samples.front() : *(upper - 1);
    const SkeletalClip *leftClip = resolver(left.clipPath);
    const SkeletalClip *rightClip = resolver(right.clipPath);
    if (!leftClip && !rightClip)
        return std::vector<BonePose>(boneCount);
    if (!leftClip)
        return rightClip->Sample(time, boneCount);
    if (!rightClip || &left == &right)
        return leftClip->Sample(time, boneCount);
    auto result = leftClip->Sample(time, boneCount);
    const auto rightPose = rightClip->Sample(time, boneCount);
    const float alpha = std::clamp((value - left.threshold) / (right.threshold - left.threshold), 0.0f, 1.0f);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = Lerp(result[index], rightPose[index], alpha);
    return result;
}

std::string BlendTree1D::DominantClip(const float value) const
{
    if (!IsValid())
        return {};
    return std::min_element(samples.begin(), samples.end(),
                            [value](const BlendTreeSample &left, const BlendTreeSample &right) {
                                return std::abs(left.threshold - value) < std::abs(right.threshold - value);
                            })
        ->clipPath;
}

bool SaveBlendTreeAsset(const BlendTree1D &tree, const std::filesystem::path &path)
{
    if (!tree.IsValid())
        return false;
    std::ostringstream output;
    output << std::setprecision(9) << "TINY_BLENDTREE 1\nparameter " << std::quoted(tree.parameter) << '\n';
    for (const auto &sample : tree.samples)
        output << "sample " << sample.threshold << ' ' << std::quoted(sample.clipPath) << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}

bool LoadBlendTreeAsset(const std::filesystem::path &path, BlendTree1D &tree)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string magic, token;
    int version = 0;
    BlendTree1D result;
    if (!(input >> magic >> version) || magic != "TINY_BLENDTREE" || version != 1)
        return false;
    while (input >> token)
    {
        if (token == "parameter")
            input >> std::quoted(result.parameter);
        else if (token == "sample")
        {
            BlendTreeSample sample;
            input >> sample.threshold >> std::quoted(sample.clipPath);
            result.samples.push_back(std::move(sample));
        }
        else
            return false;
        if (!input)
            return false;
    }
    std::sort(result.samples.begin(), result.samples.end(),
              [](const BlendTreeSample &left, const BlendTreeSample &right) { return left.threshold < right.threshold; });
    if (!result.IsValid())
        return false;
    tree = std::move(result);
    return true;
}

bool BlendTree2D::IsValid() const
{
    if (parameterX.empty() || parameterY.empty() || samples.empty())
        return false;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (samples[index].clipPath.empty())
            return false;
        for (std::size_t other = index + 1; other < samples.size(); ++other)
            if (std::abs(samples[index].position.x - samples[other].position.x) < 0.0001f &&
                std::abs(samples[index].position.y - samples[other].position.y) < 0.0001f)
                return false;
    }
    return true;
}

std::vector<float> BlendTree2D::CalculateWeights(const Vec2 &value) const
{
    std::vector<float> weights(samples.size());
    if (!IsValid())
        return weights;
    float sum = 0.0f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const float x = value.x - samples[index].position.x;
        const float y = value.y - samples[index].position.y;
        const float distanceSquared = x * x + y * y;
        if (distanceSquared < 0.000001f)
        {
            weights.assign(samples.size(), 0.0f);
            weights[index] = 1.0f;
            return weights;
        }
        weights[index] = 1.0f / distanceSquared;
        sum += weights[index];
    }
    for (float &weight : weights)
        weight /= sum;
    return weights;
}

std::vector<BonePose> BlendTree2D::Sample(const Vec2 &value, const float time, const std::size_t boneCount,
                                          const SkeletalClipResolver &resolver) const
{
    std::vector<BonePose> result(boneCount);
    if (!resolver)
        return result;
    const auto weights = CalculateWeights(value);
    float validWeight = 0.0f;
    for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
    {
        const SkeletalClip *clip = resolver(samples[sampleIndex].clipPath);
        if (!clip || weights[sampleIndex] <= 0.0f)
            continue;
        const auto pose = clip->Sample(time, boneCount);
        for (std::size_t bone = 0; bone < boneCount; ++bone)
        {
            result[bone].position += pose[bone].position * weights[sampleIndex];
            result[bone].rotation += pose[bone].rotation * weights[sampleIndex];
            result[bone].scale += (pose[bone].scale - Vec3{1.0f, 1.0f, 1.0f}) * weights[sampleIndex];
        }
        validWeight += weights[sampleIndex];
    }
    if (validWeight > 0.0f && validWeight < 0.9999f)
        for (auto &bone : result)
        {
            bone.position = bone.position / validWeight;
            bone.rotation = bone.rotation / validWeight;
            bone.scale = Vec3{1.0f, 1.0f, 1.0f} + (bone.scale - Vec3{1.0f, 1.0f, 1.0f}) / validWeight;
        }
    return result;
}

// 名称是资产间最稳定的弱标识；映射同时记录参考姿势差值，运行时无需修改源 Clip。
bool SkeletonRetargeter::BuildNameMap(const Skeleton &source, const Skeleton &target)
{
    if (!source.IsValid() || !target.IsValid())
        return false;
    m_mapping.clear();
    for (std::size_t targetIndex = 0; targetIndex < target.bones.size(); ++targetIndex)
    {
        const int sourceIndex = source.FindBone(target.bones[targetIndex].name);
        if (sourceIndex >= 0)
            m_mapping.push_back({sourceIndex, static_cast<int>(targetIndex), 1.0f});
    }
    return !m_mapping.empty();
}

void SkeletonRetargeter::SetMap(std::vector<RetargetBoneMap> mapping)
{
    m_mapping = std::move(mapping);
}

std::vector<BonePose> SkeletonRetargeter::Retarget(const std::vector<BonePose> &sourcePose, const std::size_t targetBoneCount) const
{
    std::vector<BonePose> result(targetBoneCount);
    for (const auto &entry : m_mapping)
        if (entry.sourceBone >= 0 && entry.targetBone >= 0 && static_cast<std::size_t>(entry.sourceBone) < sourcePose.size() &&
            static_cast<std::size_t>(entry.targetBone) < result.size())
        {
            result[entry.targetBone] = sourcePose[entry.sourceBone];
            result[entry.targetBone].position = result[entry.targetBone].position * entry.translationScale;
        }
    return result;
}

std::vector<BonePose> BlendAnimationLayers(const std::vector<BonePose> &basePose, const std::vector<AnimationLayer> &layers)
{
    auto result = basePose;
    for (const auto &layer : layers)
        for (std::size_t bone = 0; bone < result.size() && bone < layer.pose.size(); ++bone)
        {
            const float mask = bone < layer.boneMask.size() ? layer.boneMask[bone] : 1.0f;
            const float weight = std::clamp(layer.weight * mask, 0.0f, 1.0f);
            if (layer.additive)
            {
                result[bone].position += layer.pose[bone].position * weight;
                result[bone].rotation += layer.pose[bone].rotation * weight;
                result[bone].scale += (layer.pose[bone].scale - Vec3{1.0f, 1.0f, 1.0f}) * weight;
            }
            else
                result[bone] = Lerp(result[bone], layer.pose[bone], weight);
        }
    return result;
}

// 二骨骼 IK 在目标超出可达范围时钳制长度，并用 poleVector 固定弯曲平面以避免翻转。
bool SolveTwoBoneIk(const Skeleton &skeleton, std::vector<BonePose> &localPose, const int rootBone, const int middleBone, const int endBone,
                    const Vec3 &target, const Vec3 &pole, const float weight)
{
    if (!skeleton.IsValid() || localPose.size() != skeleton.bones.size() || rootBone < 0 || middleBone < 0 || endBone < 0 ||
        static_cast<std::size_t>(endBone) >= localPose.size() || skeleton.bones[middleBone].parent != rootBone ||
        skeleton.bones[endBone].parent != middleBone)
        return false;
    const auto debug = BuildAnimationDebugSnapshot(skeleton, localPose);
    const Vec3 root = debug.jointPositions[rootBone];
    const float upper = std::max(0.0001f, Length(localPose[middleBone].position));
    const float lower = std::max(0.0001f, Length(localPose[endBone].position));
    Vec3 direction = Normalize(target - root);
    const float distance = std::clamp(Length(target - root), 0.0001f, upper + lower - 0.0001f);
    Vec3 bendNormal = Normalize(Cross(direction, pole - root));
    if (LengthSquared(bendNormal) < 0.0001f)
        bendNormal = {0.0f, 0.0f, 1.0f};
    const Vec3 bend = Normalize(Cross(bendNormal, direction));
    const float projected = (upper * upper + distance * distance - lower * lower) / (2.0f * distance);
    const float height = std::sqrt(std::max(0.0f, upper * upper - projected * projected));
    const Vec3 joint = root + direction * projected + bend * height;
    const Vec3 newUpper = joint - root;
    const Vec3 newLower = target - joint;
    const float alpha = std::clamp(weight, 0.0f, 1.0f);
    const auto eulerFromDirection = [](const Vec3 &value) {
        const Vec3 normal = Normalize(value);
        return Vec3{0.0f, -std::asin(std::clamp(normal.z, -1.0f, 1.0f)) * 180.0f / Pi, std::atan2(normal.y, normal.x) * 180.0f / Pi};
    };
    localPose[rootBone].rotation = Lerp(localPose[rootBone].rotation, eulerFromDirection(newUpper), alpha);
    const Vec3 worldLowerRotation = eulerFromDirection(newLower);
    localPose[middleBone].rotation = Lerp(localPose[middleBone].rotation, worldLowerRotation - eulerFromDirection(newUpper), alpha);
    return true;
}

AnimationDebugSnapshot BuildAnimationDebugSnapshot(const Skeleton &skeleton, const std::vector<BonePose> &localPose)
{
    AnimationDebugSnapshot result;
    if (!skeleton.IsValid() || localPose.size() != skeleton.bones.size())
        return result;
    std::vector<Mat4> world(skeleton.bones.size());
    result.jointPositions.resize(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const auto &pose = localPose[index];
        const Mat4 local = TransformMatrix(pose.position, pose.rotation, pose.scale);
        const int parent = skeleton.bones[index].parent;
        world[index] = parent >= 0 ? world[static_cast<std::size_t>(parent)] * local : local;
        result.jointPositions[index] = {world[index].At(0, 3), world[index].At(1, 3), world[index].At(2, 3)};
        if (parent >= 0)
            result.bones.push_back(
                {result.jointPositions[static_cast<std::size_t>(parent)], result.jointPositions[index], static_cast<int>(index)});
    }
    return result;
}

bool AnimationGraph::IsValid() const
{
    if (entryState.empty() || states.empty() || !FindState(entryState))
        return false;
    std::unordered_set<std::string> stateNames, parameterNames;
    for (const auto &parameter : parameters)
        if (parameter.name.empty() || !parameterNames.insert(parameter.name).second)
            return false;
    for (const auto &state : states)
        if (state.name.empty() || state.motionPath.empty() || !stateNames.insert(state.name).second)
            return false;
    for (const auto &transition : transitions)
        if (!stateNames.count(transition.from) || !stateNames.count(transition.to) || !parameterNames.count(transition.parameter) ||
            transition.blendSeconds < 0.0f)
            return false;
    return true;
}

const AnimationGraphState *AnimationGraph::FindState(const std::string &name) const
{
    const auto found = std::find_if(states.begin(), states.end(), [&name](const AnimationGraphState &state) { return state.name == name; });
    return found == states.end() ? nullptr : &*found;
}

bool SaveAnimationGraphAsset(const AnimationGraph &graph, const std::filesystem::path &path)
{
    if (!graph.IsValid())
        return false;
    std::ostringstream output;
    output << std::setprecision(9) << "TINY_ANIMGRAPH 1\nentry " << std::quoted(graph.entryState) << '\n';
    for (const auto &parameter : graph.parameters)
        output << "parameter " << std::quoted(parameter.name) << ' ' << parameter.defaultValue << '\n';
    for (const auto &state : graph.states)
        output << "state " << std::quoted(state.name) << ' ' << std::quoted(state.motionPath) << '\n';
    for (const auto &transition : graph.transitions)
        output << "transition " << std::quoted(transition.from) << ' ' << std::quoted(transition.to) << ' '
               << std::quoted(transition.parameter) << ' ' << static_cast<int>(transition.comparison) << ' ' << transition.threshold << ' '
               << transition.blendSeconds << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}

bool LoadAnimationGraphAsset(const std::filesystem::path &path, AnimationGraph &graph)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string magic, token;
    int version = 0;
    AnimationGraph result;
    if (!(input >> magic >> version) || magic != "TINY_ANIMGRAPH" || version != 1)
        return false;
    while (input >> token)
    {
        if (token == "entry")
            input >> std::quoted(result.entryState);
        else if (token == "parameter")
        {
            AnimationGraphParameter parameter;
            input >> std::quoted(parameter.name) >> parameter.defaultValue;
            result.parameters.push_back(std::move(parameter));
        }
        else if (token == "state")
        {
            AnimationGraphState state;
            input >> std::quoted(state.name) >> std::quoted(state.motionPath);
            result.states.push_back(std::move(state));
        }
        else if (token == "transition")
        {
            AnimationGraphTransition transition;
            int comparison = 0;
            input >> std::quoted(transition.from) >> std::quoted(transition.to) >> std::quoted(transition.parameter) >> comparison >>
                transition.threshold >> transition.blendSeconds;
            if (comparison < 0 || comparison > static_cast<int>(AnimationGraphComparison::NotEqual))
                return false;
            transition.comparison = static_cast<AnimationGraphComparison>(comparison);
            result.transitions.push_back(std::move(transition));
        }
        else
            return false;
        if (!input)
            return false;
    }
    if (!result.IsValid())
        return false;
    graph = std::move(result);
    return true;
}

// Player 持有不可变图定义并重置所有运行态，防止旧状态名或播放时间泄漏到新图。
bool AnimationGraphPlayer::SetGraph(std::shared_ptr<const AnimationGraph> graph)
{
    if (!graph || !graph->IsValid())
        return false;
    m_graph = std::move(graph);
    m_parameters.clear();
    for (const auto &parameter : m_graph->parameters)
        m_parameters[parameter.name] = parameter.defaultValue;
    m_state = m_graph->entryState;
    m_previousState.clear();
    m_time = m_previousTime = m_blendTime = m_blendDuration = 0.0f;
    return true;
}

void AnimationGraphPlayer::SetParameter(const std::string &name, const float value)
{
    if (m_graph && m_parameters.count(name))
        m_parameters[name] = value;
}

std::vector<BonePose> AnimationGraphPlayer::Update(const float deltaTime, const std::size_t boneCount, const MotionSampler &sampler)
{
    if (!m_graph || !sampler)
        return std::vector<BonePose>(boneCount);
    for (const auto &transition : m_graph->transitions)
        if (transition.from == m_state && Compare(m_parameters[transition.parameter], transition.comparison, transition.threshold))
        {
            m_previousState = m_state;
            m_previousTime = m_time;
            m_state = transition.to;
            m_time = 0.0f;
            m_blendDuration = transition.blendSeconds;
            m_blendTime = 0.0f;
            break;
        }
    const float step = std::max(0.0f, deltaTime);
    m_time += step;
    auto pose = sampler(m_graph->FindState(m_state)->motionPath, m_time, boneCount);
    if (!m_previousState.empty() && m_blendDuration > 0.0f)
    {
        m_previousTime += step;
        m_blendTime += step;
        const auto previous = sampler(m_graph->FindState(m_previousState)->motionPath, m_previousTime, boneCount);
        const float alpha = std::clamp(m_blendTime / m_blendDuration, 0.0f, 1.0f);
        for (std::size_t index = 0; index < pose.size(); ++index)
            pose[index] = Lerp(previous[index], pose[index], alpha);
        if (alpha >= 1.0f)
            m_previousState.clear();
    }
    else if (!m_previousState.empty())
        m_previousState.clear();
    return pose;
}

const std::string &AnimationGraphPlayer::GetState() const
{
    return m_state;
}

float AnimationGraphPlayer::GetTime() const
{
    return m_time;
}

bool AnimationStateMachine::AddState(std::string name, std::shared_ptr<SkeletalClip> clip)
{
    if (name.empty() || !clip)
        return false;
    const bool inserted = m_states.emplace(name, std::move(clip)).second;
    if (inserted && m_state.empty())
        m_state = std::move(name);
    return inserted;
}

bool AnimationStateMachine::AddTransition(std::string from, std::string to, const float blendSeconds, Condition condition)
{
    if (!m_states.count(from) || !m_states.count(to) || !condition)
        return false;
    m_transitions.push_back({std::move(from), std::move(to), std::max(0.0f, blendSeconds), std::move(condition)});
    return true;
}

bool AnimationStateMachine::SetState(const std::string &name)
{
    if (!m_states.count(name))
        return false;
    m_previousState.clear();
    m_state = name;
    m_time = 0.0f;
    return true;
}

std::vector<BonePose> AnimationStateMachine::Update(const float deltaTime, const std::size_t boneCount)
{
    if (m_state.empty())
        return std::vector<BonePose>(boneCount);
    for (const auto &transition : m_transitions)
        if (transition.from == m_state && transition.condition())
        {
            m_previousState = m_state;
            m_previousTime = m_time;
            m_state = transition.to;
            m_time = 0.0f;
            m_blendDuration = transition.blendSeconds;
            m_blendTime = 0.0f;
            break;
        }
    m_time += std::max(0.0f, deltaTime);
    auto pose = m_states.at(m_state)->Sample(m_time, boneCount);
    if (!m_previousState.empty() && m_blendDuration > 0.0f)
    {
        m_previousTime += std::max(0.0f, deltaTime);
        m_blendTime += std::max(0.0f, deltaTime);
        const auto previous = m_states.at(m_previousState)->Sample(m_previousTime, boneCount);
        const float alpha = std::clamp(m_blendTime / m_blendDuration, 0.0f, 1.0f);
        for (std::size_t index = 0; index < pose.size(); ++index)
            pose[index] = Lerp(previous[index], pose[index], alpha);
        if (alpha >= 1.0f)
            m_previousState.clear();
    }
    return pose;
}

const std::string &AnimationStateMachine::GetState() const
{
    return m_state;
}
} // namespace MiniEngine
