#include "MiniEngine/Render/EnvironmentBaker.hpp"

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace MiniEngine
{
namespace
{
float RadicalInverse(std::uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec3 Direction(const float u, const float v)
{
    const float longitude = (u - 0.5f) * 2.0f * Pi, latitude = (v - 0.5f) * Pi;
    const float cosine = std::cos(latitude);
    return {cosine * std::cos(longitude), std::sin(latitude), cosine * std::sin(longitude)};
}

Vec3 Sample(const TextureResource &texture, const Vec3 &direction)
{
    const Vec3 value = Normalize(direction);
    const float u = std::atan2(value.z, value.x) / (2.0f * Pi) + 0.5f;
    const float v = std::asin(std::clamp(value.y, -1.0f, 1.0f)) / Pi + 0.5f;
    const int x = std::clamp(static_cast<int>(u * texture.GetWidth()), 0, texture.GetWidth() - 1);
    const int y = std::clamp(static_cast<int>(v * texture.GetHeight()), 0, texture.GetHeight() - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * texture.GetWidth() + x) * 4;
    if (texture.IsHdr())
        return {texture.GetFloatData()[offset], texture.GetFloatData()[offset + 1], texture.GetFloatData()[offset + 2]};
    return {texture.GetData()[offset] / 255.0f, texture.GetData()[offset + 1] / 255.0f, texture.GetData()[offset + 2] / 255.0f};
}

void Basis(const Vec3 &normal, Vec3 &tangent, Vec3 &bitangent)
{
    tangent = Normalize(std::abs(normal.y) < 0.999f ? Cross({0.0f, 1.0f, 0.0f}, normal) : Cross({1.0f, 0.0f, 0.0f}, normal));
    bitangent = Cross(normal, tangent);
}

Vec3 ImportanceSampleGgx(const float u, const float v, const float roughness, const Vec3 &normal)
{
    const float a = roughness * roughness;
    const float phi = 2.0f * Pi * u;
    const float cosine = std::sqrt((1.0f - v) / std::max(0.0001f, 1.0f + (a * a - 1.0f) * v));
    const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    Vec3 tangent, bitangent;
    Basis(normal, tangent, bitangent);
    return Normalize(tangent * (std::cos(phi) * sine) + bitangent * (std::sin(phi) * sine) + normal * cosine);
}

bool SaveTexture(const std::filesystem::path &path, const int width, const int height, const std::vector<float> &pixels)
{
    BinaryWriter writer;
    writer.WriteBytes("TNYTEX1\0", 8);
    writer.WriteU32(static_cast<std::uint32_t>(width));
    writer.WriteU32(static_cast<std::uint32_t>(height));
    writer.WriteBool(true);
    writer.WriteU64(pixels.size());
    for (const float value : pixels)
        writer.WriteFloat(value);
    return writer.Save(path);
}

void Store(std::vector<float> &pixels, const int width, const int x, const int y, const Vec3 &color, const float alpha = 1.0f)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
    pixels[offset] = color.x;
    pixels[offset + 1] = color.y;
    pixels[offset + 2] = color.z;
    pixels[offset + 3] = alpha;
}
} // namespace

EnvironmentBakeResult EnvironmentBaker::Bake(const std::filesystem::path &source, const std::filesystem::path &outputDirectory,
                                             const int sampleCount)
{
    EnvironmentBakeResult result;
    TextureResource environment;
    if (!environment.Load(source.generic_string()))
    {
        result.error = environment.GetError();
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
    constexpr int irradianceWidth = 32, irradianceHeight = 16;
    std::vector<float> irradiance(irradianceWidth * irradianceHeight * 4);
    for (int y = 0; y < irradianceHeight; ++y)
        for (int x = 0; x < irradianceWidth; ++x)
        {
            const Vec3 normal = Direction((x + 0.5f) / irradianceWidth, (y + 0.5f) / irradianceHeight);
            Vec3 tangent, bitangent, color{};
            Basis(normal, tangent, bitangent);
            for (int sample = 0; sample < samples; ++sample)
            {
                const float u = (sample + 0.5f) / samples, v = RadicalInverse(static_cast<std::uint32_t>(sample));
                const float radius = std::sqrt(u), phi = 2.0f * Pi * v;
                const Vec3 direction = Normalize(tangent * (radius * std::cos(phi)) + bitangent * (radius * std::sin(phi)) +
                                                 normal * std::sqrt(std::max(0.0f, 1.0f - u)));
                color += Sample(environment, direction);
            }
            Store(irradiance, irradianceWidth, x, y, color / static_cast<float>(samples));
        }

    constexpr int prefilterWidth = 128, prefilterSliceHeight = 64, levels = 6;
    std::vector<float> prefilter(prefilterWidth * prefilterSliceHeight * levels * 4);
    for (int level = 0; level < levels; ++level)
    {
        const float roughness = static_cast<float>(level) / (levels - 1);
        for (int y = 0; y < prefilterSliceHeight; ++y)
            for (int x = 0; x < prefilterWidth; ++x)
            {
                const Vec3 normal = Direction((x + 0.5f) / prefilterWidth, (y + 0.5f) / prefilterSliceHeight);
                Vec3 color{};
                float weight = 0.0f;
                for (int sample = 0; sample < samples; ++sample)
                {
                    const Vec3 halfway = ImportanceSampleGgx((sample + 0.5f) / samples, RadicalInverse(static_cast<std::uint32_t>(sample)),
                                                             std::max(0.02f, roughness), normal);
                    const Vec3 light = Normalize(halfway * (2.0f * Dot(normal, halfway)) - normal);
                    const float nl = std::max(0.0f, Dot(normal, light));
                    if (nl > 0.0f)
                    {
                        color += Sample(environment, light) * nl;
                        weight += nl;
                    }
                }
                Store(prefilter, prefilterWidth, x, level * prefilterSliceHeight + y, color / std::max(weight, 0.0001f));
            }
    }

    constexpr int brdfSize = 128;
    std::vector<float> brdf(brdfSize * brdfSize * 4);
    for (int y = 0; y < brdfSize; ++y)
        for (int x = 0; x < brdfSize; ++x)
        {
            const float nv = std::max(0.001f, (x + 0.5f) / brdfSize), roughness = (y + 0.5f) / brdfSize;
            const Vec3 normal{0.0f, 0.0f, 1.0f}, view{std::sqrt(std::max(0.0f, 1.0f - nv * nv)), 0.0f, nv};
            float scale = 0.0f, bias = 0.0f;
            for (int sample = 0; sample < samples; ++sample)
            {
                const Vec3 halfway =
                    ImportanceSampleGgx((sample + 0.5f) / samples, RadicalInverse(static_cast<std::uint32_t>(sample)), roughness, normal);
                const Vec3 light = Normalize(halfway * (2.0f * Dot(view, halfway)) - view);
                const float nl = std::max(light.z, 0.0f), nh = std::max(halfway.z, 0.0f), vh = std::max(Dot(view, halfway), 0.0f);
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
            Store(brdf, brdfSize, x, y, {scale / samples, bias / samples, 0.0f});
        }

    const std::string base = source.stem().string();
    result.irradiancePath = outputDirectory / (base + ".irradiance.hdr");
    result.prefilterPath = outputDirectory / (base + ".prefilter.hdr");
    result.brdfLutPath = outputDirectory / (base + ".brdf.hdr");
    result.environmentPath = outputDirectory / (base + ".env");
    if (!SaveTexture(result.irradiancePath, irradianceWidth, irradianceHeight, irradiance) ||
        !SaveTexture(result.prefilterPath, prefilterWidth, prefilterSliceHeight * levels, prefilter) ||
        !SaveTexture(result.brdfLutPath, brdfSize, brdfSize, brdf))
    {
        result.error = "Cannot write baked environment textures";
        return result;
    }
    std::ostringstream manifest;
    manifest << "TINY_ENV 1\nirradiance " << std::quoted(result.irradiancePath.filename().generic_string()) << "\nprefilter "
             << std::quoted(result.prefilterPath.filename().generic_string()) << "\nbrdf "
             << std::quoted(result.brdfLutPath.filename().generic_string()) << '\n';
    result.success = PlatformFile::WriteAllText(result.environmentPath, manifest.str());
    if (!result.success)
        result.error = "Cannot write environment manifest";
    return result;
}
} // namespace MiniEngine
