// 文件：src/render/environment_baker.cpp
// 作用：离线生成环境光照所需的辐照度、预过滤立方体贴图和 BRDF 查找表。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "tinyengine/render/environment_baker.h"

#include "tinyengine/math/math.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/resource.h"
#include "tinyengine/serialization/archive.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace tinyengine
{
namespace
{
float radicalInverse(std::uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec3 cubeDirection(const int face, const float u, const float v)
{
    const float x = u * 2.0f - 1.0f, y = v * 2.0f - 1.0f;
    if (face == 0)
        return normalize({1.0f, -y, -x});
    if (face == 1)
        return normalize({-1.0f, -y, x});
    if (face == 2)
        return normalize({x, 1.0f, y});
    if (face == 3)
        return normalize({x, -1.0f, -y});
    if (face == 4)
        return normalize({x, -y, 1.0f});
    return normalize({-x, -y, -1.0f});
}

Vec3 sample(const TextureResource& texture, const Vec3& direction)
{
    const Vec3 value = normalize(direction);
    const float u = std::atan2(value.z, value.x) / (2.0f * kPi) + 0.5f;
    const float v = std::asin(std::clamp(value.y, -1.0f, 1.0f)) / kPi + 0.5f;
    const int x = std::clamp(static_cast<int>(u * texture.getWidth()), 0, texture.getWidth() - 1);
    const int y = std::clamp(static_cast<int>(v * texture.getHeight()), 0, texture.getHeight() - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * texture.getWidth() + x) * 4;
    if (texture.isHdr())
        return {texture.GetFloatData()[offset], texture.GetFloatData()[offset + 1], texture.GetFloatData()[offset + 2]};
    return {texture.GetData()[offset] / 255.0f, texture.GetData()[offset + 1] / 255.0f,
            texture.GetData()[offset + 2] / 255.0f};
}

void basis(const Vec3& normal, Vec3& tangent, Vec3& bitangent)
{
    tangent =
        normalize(std::abs(normal.y) < 0.999f ? cross({0.0f, 1.0f, 0.0f}, normal) : cross({1.0f, 0.0f, 0.0f}, normal));
    bitangent = cross(normal, tangent);
}

Vec3 importanceSampleGgx(const float u, const float v, const float roughness, const Vec3& normal)
{
    const float a = roughness * roughness;
    const float phi = 2.0f * kPi * u;
    const float cosine = std::sqrt((1.0f - v) / std::max(0.0001f, 1.0f + (a * a - 1.0f) * v));
    const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    Vec3 tangent, bitangent;
    basis(normal, tangent, bitangent);
    return normalize(tangent * (std::cos(phi) * sine) + bitangent * (std::sin(phi) * sine) + normal * cosine);
}

bool saveTexture(const std::filesystem::path& path, const int width, const int height, const std::vector<float>& pixels)
{
    BinaryWriter writer;
    writer.writeBytes("TNYTEX1\0", 8);
    writer.writeU32(static_cast<std::uint32_t>(width));
    writer.writeU32(static_cast<std::uint32_t>(height));
    writer.writeBool(true);
    writer.writeU64(pixels.size());
    for (const float value : pixels)
        writer.writeFloat(value);
    return writer.Save(path);
}

void store(std::vector<float>& pixels, const int width, const int x, const int y, const Vec3& color,
           const float alpha = 1.0f)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
    pixels[offset] = color.x;
    pixels[offset + 1] = color.y;
    pixels[offset + 2] = color.z;
    pixels[offset + 3] = alpha;
}
} // namespace

// 使用确定性的低差异采样离线积分；运行时只读取结果，避免每次启动重复昂贵卷积。
EnvironmentBakeResult EnvironmentBaker::bake(const std::filesystem::path& source,
                                             const std::filesystem::path& outputDirectory, const int sampleCount)
{
    EnvironmentBakeResult result;
    TextureResource environment;
    if (!environment.load(source.generic_string()))
    {
        result.error = environment.getError();
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(outputDirectory, error);
    if (error)
    {
        result.error = error.message();
        return result;
    }
    const int samples = std::clamp(sampleCount, 16, 1024);
    constexpr int kIrradianceFaceSize = 16, kCubeFaces = 6;
    constexpr int kIrradianceWidth = kIrradianceFaceSize * kCubeFaces, kIrradianceHeight = kIrradianceFaceSize;
    std::vector<float> irradiance(kIrradianceWidth * kIrradianceHeight * 4);
    for (int y = 0; y < kIrradianceHeight; ++y)
        for (int x = 0; x < kIrradianceWidth; ++x)
        {
            const int face = x / kIrradianceFaceSize, faceX = x % kIrradianceFaceSize;
            const Vec3 normal =
                cubeDirection(face, (faceX + 0.5f) / kIrradianceFaceSize, (y + 0.5f) / kIrradianceFaceSize);
            Vec3 tangent, bitangent, color{};
            basis(normal, tangent, bitangent);
            for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
            {
                const float u = (sampleIndex + 0.5f) / samples,
                            v = radicalInverse(static_cast<std::uint32_t>(sampleIndex));
                const float radius = std::sqrt(u), phi = 2.0f * kPi * v;
                const Vec3 direction =
                    normalize(tangent * (radius * std::cos(phi)) + bitangent * (radius * std::sin(phi)) +
                              normal * std::sqrt(std::max(0.0f, 1.0f - u)));
                color += sample(environment, direction);
            }
            store(irradiance, kIrradianceWidth, x, y, color / static_cast<float>(samples));
        }

    constexpr int kPrefilterFaceSize = 32, kPrefilterWidth = kPrefilterFaceSize * kCubeFaces,
                  kPrefilterSliceHeight = kPrefilterFaceSize, kLevels = 6;
    std::vector<float> prefilter(kPrefilterWidth * kPrefilterSliceHeight * kLevels * 4);
    for (int level = 0; level < kLevels; ++level)
    {
        const float roughness = static_cast<float>(level) / (kLevels - 1);
        for (int y = 0; y < kPrefilterSliceHeight; ++y)
            for (int x = 0; x < kPrefilterWidth; ++x)
            {
                const int face = x / kPrefilterFaceSize, faceX = x % kPrefilterFaceSize;
                const Vec3 normal =
                    cubeDirection(face, (faceX + 0.5f) / kPrefilterFaceSize, (y + 0.5f) / kPrefilterFaceSize);
                Vec3 color{};
                float weight = 0.0f;
                for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
                {
                    const Vec3 halfway = importanceSampleGgx((sampleIndex + 0.5f) / samples,
                                                             radicalInverse(static_cast<std::uint32_t>(sampleIndex)),
                                                             std::max(0.02f, roughness), normal);
                    const Vec3 light = normalize(halfway * (2.0f * dot(normal, halfway)) - normal);
                    const float nl = std::max(0.0f, dot(normal, light));
                    if (nl > 0.0f)
                    {
                        color += sample(environment, light) * nl;
                        weight += nl;
                    }
                }
                store(prefilter, kPrefilterWidth, x, level * kPrefilterSliceHeight + y,
                      color / std::max(weight, 0.0001f));
            }
    }

    constexpr int kBrdfSize = 128;
    std::vector<float> brdf(kBrdfSize * kBrdfSize * 4);
    for (int y = 0; y < kBrdfSize; ++y)
        for (int x = 0; x < kBrdfSize; ++x)
        {
            const float nv = std::max(0.001f, (x + 0.5f) / kBrdfSize), roughness = (y + 0.5f) / kBrdfSize;
            const Vec3 normal{0.0f, 0.0f, 1.0f}, view{std::sqrt(std::max(0.0f, 1.0f - nv * nv)), 0.0f, nv};
            float scale = 0.0f, bias = 0.0f;
            for (int sample = 0; sample < samples; ++sample)
            {
                const Vec3 halfway = importanceSampleGgx(
                    (sample + 0.5f) / samples, radicalInverse(static_cast<std::uint32_t>(sample)), roughness, normal);
                const Vec3 light = normalize(halfway * (2.0f * dot(view, halfway)) - view);
                const float nl = std::max(light.z, 0.0f), nh = std::max(halfway.z, 0.0f),
                            vh = std::max(dot(view, halfway), 0.0f);
                if (nl > 0.0f)
                {
                    const float k = roughness * roughness * 0.5f;
                    const float gv = nv / (nv * (1.0f - k) + k), gl = nl / (nl * (1.0f - k) + k);
                    const float visibility = gv * gl * vh / std::max(nh * nv, 0.0001f);
                    const float fresnel = std::pow(1.0f - vh, 5.0f);
                    scale += (1.0f - fresnel) * visibility;
                    bias += fresnel * visibility;
                }
            }
            store(brdf, kBrdfSize, x, y, {scale / samples, bias / samples, 0.0f});
        }

    const std::string base = source.stem().string();
    result.irradiancePath = outputDirectory / (base + ".irradiance.hdr");
    result.prefilterPath = outputDirectory / (base + ".prefilter.hdr");
    result.brdfLutPath = outputDirectory / (base + ".brdf.hdr");
    result.environmentPath = outputDirectory / (base + ".env");
    if (!saveTexture(result.irradiancePath, kIrradianceWidth, kIrradianceHeight, irradiance) ||
        !saveTexture(result.prefilterPath, kPrefilterWidth, kPrefilterSliceHeight * kLevels, prefilter) ||
        !saveTexture(result.brdfLutPath, kBrdfSize, kBrdfSize, brdf))
    {
        result.error = "Cannot write baked environment textures";
        return result;
    }
    std::error_code relativeError;
    auto skyPath = std::filesystem::relative(source, outputDirectory, relativeError);
    if (relativeError || skyPath.empty())
        skyPath = source;
    std::ostringstream manifest;
    manifest << "TINY_ENV 1\nsky " << std::quoted(skyPath.generic_string()) << "\nirradiance "
             << std::quoted(result.irradiancePath.filename().generic_string()) << "\nprefilter "
             << std::quoted(result.prefilterPath.filename().generic_string()) << "\nbrdf "
             << std::quoted(result.brdfLutPath.filename().generic_string()) << '\n';
    result.success = PlatformFile::writeAllText(result.environmentPath, manifest.str());
    if (!result.success)
        result.error = "Cannot write environment manifest";
    return result;
}
} // namespace tinyengine
