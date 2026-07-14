#include "MiniEngine/Resource/Resource.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#endif

namespace MiniEngine
{
namespace
{
#ifdef _WIN32
bool LoadWicTexture(const std::vector<std::uint8_t> &bytes, int &width, int &height, std::vector<std::uint8_t> &pixels)
{
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
        return false;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory *factory = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    bool success = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(
            converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT imageWidth = 0, imageHeight = 0;
        if (SUCCEEDED(converter->GetSize(&imageWidth, &imageHeight)) && imageWidth > 0 && imageHeight > 0 && imageWidth <= 32768 &&
            imageHeight <= 32768 && static_cast<std::uint64_t>(imageWidth) * imageHeight * 4 <= std::numeric_limits<UINT>::max())
        {
            pixels.resize(static_cast<std::size_t>(imageWidth) * imageHeight * 4);
            success = SUCCEEDED(converter->CopyPixels(nullptr, imageWidth * 4, static_cast<UINT>(pixels.size()), pixels.data()));
            if (success)
            {
                width = static_cast<int>(imageWidth);
                height = static_cast<int>(imageHeight);
            }
        }
    }
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (!success)
        pixels.clear();
    return success;
}
#endif

bool LoadRadianceHdr(const std::vector<std::uint8_t> &bytes, int &width, int &height, std::vector<float> &pixels)
{
    if (bytes.size() < 16)
        return false;
    std::size_t offset = 0;
    std::string line, resolution;
    auto readLine = [&]() {
        line.clear();
        while (offset < bytes.size() && bytes[offset] != '\n')
            line.push_back(static_cast<char>(bytes[offset++]));
        if (offset < bytes.size())
            ++offset;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return !line.empty() || offset <= bytes.size();
    };
    if (!readLine() || line.rfind("#?", 0) != 0)
        return false;
    while (offset < bytes.size())
    {
        readLine();
        if (line.empty())
            break;
    }
    if (!readLine())
        return false;
    resolution = line;
    char ySign = '-', xSign = '+';
    char yAxis = 'Y', xAxis = 'X';
    std::istringstream dimensions(resolution);
    if (!(dimensions >> ySign >> yAxis >> height >> xSign >> xAxis >> width) || yAxis != 'Y' || xAxis != 'X' || width <= 0 || height <= 0 ||
        width > 32768 || height > 32768)
        return false;
    std::vector<std::uint8_t> rgbe(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        std::uint8_t *scanline = rgbe.data() + static_cast<std::size_t>(y) * width * 4;
        if (width >= 8 && width <= 32767 && offset + 4 <= bytes.size() && bytes[offset] == 2 && bytes[offset + 1] == 2 &&
            (bytes[offset + 2] & 0x80) == 0)
        {
            const int encodedWidth = (bytes[offset + 2] << 8) | bytes[offset + 3];
            offset += 4;
            if (encodedWidth != width)
                return false;
            for (int channel = 0; channel < 4; ++channel)
            {
                int x = 0;
                while (x < width && offset < bytes.size())
                {
                    const int count = bytes[offset++];
                    if (count > 128)
                    {
                        const int run = count - 128;
                        if (run == 0 || offset >= bytes.size() || x + run > width)
                            return false;
                        const std::uint8_t value = bytes[offset++];
                        for (int i = 0; i < run; ++i)
                            scanline[(x++ * 4) + channel] = value;
                    }
                    else
                    {
                        if (count == 0 || offset + count > bytes.size() || x + count > width)
                            return false;
                        for (int i = 0; i < count; ++i)
                            scanline[(x++ * 4) + channel] = bytes[offset++];
                    }
                }
                if (x != width)
                    return false;
            }
        }
        else
        {
            const std::size_t byteCount = static_cast<std::size_t>(width) * 4;
            if (offset + byteCount > bytes.size())
                return false;
            std::copy_n(bytes.data() + offset, byteCount, scanline);
            offset += byteCount;
        }
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
    {
        const std::uint8_t exponent = rgbe[index * 4 + 3];
        for (int channel = 0; channel < 3; ++channel)
        {
            const float linear =
                exponent ? std::ldexp(static_cast<float>(rgbe[index * 4 + channel]) + 0.5f, static_cast<int>(exponent) - 136) : 0.0f;
            pixels[index * 4 + channel] = std::max(0.0f, linear);
        }
        pixels[index * 4 + 3] = 1.0f;
    }
    return true;
}
} // namespace

const std::string &Resource::GetPath() const
{
    return m_path;
}
ResourceState Resource::GetState() const
{
    return m_state.load();
}
bool Resource::IsReady() const
{
    return GetState() == ResourceState::Loaded;
}

std::string Resource::GetError() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_error;
}

bool Resource::Wait() const
{
    std::unique_lock<std::mutex> lock(m_stateMutex);
    m_stateCv.wait(lock, [this]() {
        const auto state = m_state.load();
        return state != ResourceState::Loading;
    });
    return m_state.load() == ResourceState::Loaded;
}

void Resource::MarkLoading(const std::string &path)
{
    if (!path.empty())
        m_path = path;
    SetState(ResourceState::Loading);
}

void Resource::SetState(const ResourceState state, std::string error)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_error = std::move(error);
        m_state.store(state);
    }
    m_stateCv.notify_all();
}

bool TextureResource::Load(const std::string &path)
{
    MarkLoading(path);
    m_data.clear();
    m_floatData.clear();
    m_width = m_height = 0;
    const auto bytes = PlatformFile::ReadAllBytes(path);
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".hdr")
    {
        if (!LoadRadianceHdr(bytes, m_width, m_height, m_floatData))
        {
            SetState(ResourceState::Failed, "Invalid Radiance HDR texture: " + path);
            return false;
        }
        SetState(ResourceState::Loaded);
        return true;
    }
#ifdef _WIN32
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tif" ||
        extension == ".tiff")
    {
        if (!LoadWicTexture(bytes, m_width, m_height, m_data))
        {
            SetState(ResourceState::Failed, "WIC image decode failed: " + path);
            return false;
        }
        SetState(ResourceState::Loaded);
        return true;
    }
#endif
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string magic;
    stream >> magic;
    if (magic != "P3")
    {
        SetState(ResourceState::Failed, "Only ASCII PPM (P3) textures are supported: " + path);
        return false;
    }
    auto readValue = [&stream]() {
        std::string token;
        while (stream >> token)
        {
            if (!token.empty() && token.front() == '#')
            {
                std::string ignored;
                std::getline(stream, ignored);
                continue;
            }
            return std::stoi(token);
        }
        return -1;
    };
    m_width = readValue();
    m_height = readValue();
    const int maximum = readValue();
    if (m_width <= 0 || m_height <= 0 || maximum <= 0)
    {
        SetState(ResourceState::Failed, "Invalid PPM header: " + path);
        return false;
    }
    m_data.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height); ++pixel)
    {
        const int red = readValue(), green = readValue(), blue = readValue();
        if (red < 0 || green < 0 || blue < 0)
        {
            m_data.clear();
            SetState(ResourceState::Failed, "Incomplete PPM pixels: " + path);
            return false;
        }
        m_data[pixel * 4] = static_cast<std::uint8_t>(std::clamp(red * 255 / maximum, 0, 255));
        m_data[pixel * 4 + 1] = static_cast<std::uint8_t>(std::clamp(green * 255 / maximum, 0, 255));
        m_data[pixel * 4 + 2] = static_cast<std::uint8_t>(std::clamp(blue * 255 / maximum, 0, 255));
        m_data[pixel * 4 + 3] = 255;
    }
    SetState(ResourceState::Loaded);
    return true;
}

void TextureResource::Unload()
{
    m_data.clear();
    m_floatData.clear();
    m_width = m_height = 0;
    SetState(ResourceState::Unloaded);
}
std::size_t TextureResource::GetByteSize() const
{
    return m_data.size() + m_floatData.size() * sizeof(float);
}
const std::vector<std::uint8_t> &TextureResource::GetData() const
{
    return m_data;
}
const std::vector<float> &TextureResource::GetFloatData() const
{
    return m_floatData;
}
bool TextureResource::IsHdr() const
{
    return !m_floatData.empty();
}
int TextureResource::GetWidth() const
{
    return m_width;
}
int TextureResource::GetHeight() const
{
    return m_height;
}

bool MeshResource::Load(const std::string &path)
{
    MarkLoading(path);
    std::istringstream stream(PlatformFile::ReadAllText(path));
    if (!stream)
    {
        SetState(ResourceState::Failed, "Mesh file not found: " + path);
        return false;
    }
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::string line;
    m_vertices.clear();
    m_indices.clear();
    m_bounds.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
    m_bounds.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    while (std::getline(stream, line))
    {
        std::istringstream row(line);
        std::string type;
        row >> type;
        if (type == "v")
        {
            Vec3 value;
            if (!(row >> value.x >> value.y >> value.z))
                continue;
            positions.push_back(value);
            m_bounds.minimum.x = std::min(m_bounds.minimum.x, value.x);
            m_bounds.minimum.y = std::min(m_bounds.minimum.y, value.y);
            m_bounds.minimum.z = std::min(m_bounds.minimum.z, value.z);
            m_bounds.maximum.x = std::max(m_bounds.maximum.x, value.x);
            m_bounds.maximum.y = std::max(m_bounds.maximum.y, value.y);
            m_bounds.maximum.z = std::max(m_bounds.maximum.z, value.z);
        }
        else if (type == "vt")
        {
            Vec2 value;
            if (row >> value.x >> value.y)
                uvs.push_back(value);
        }
        else if (type == "vn")
        {
            Vec3 value;
            if (row >> value.x >> value.y >> value.z)
                normals.push_back(Normalize(value));
        }
        else if (type == "f")
        {
            struct FaceIndex
            {
                int position = -1, uv = -1, normal = -1;
            };
            std::vector<FaceIndex> face;
            std::string value;
            while (row >> value)
            {
                FaceIndex parsed;
                try
                {
                    const auto firstSlash = value.find('/');
                    const auto secondSlash = firstSlash == std::string::npos ? std::string::npos : value.find('/', firstSlash + 1);
                    auto resolve = [](const std::string &token, const std::size_t size) {
                        if (token.empty())
                            return -1;
                        const int raw = std::stoi(token);
                        const int resolved = raw > 0 ? raw - 1 : static_cast<int>(size) + raw;
                        return resolved >= 0 && static_cast<std::size_t>(resolved) < size ? resolved : -1;
                    };
                    parsed.position = resolve(value.substr(0, firstSlash), positions.size());
                    if (firstSlash != std::string::npos)
                    {
                        parsed.uv = resolve(value.substr(firstSlash + 1, secondSlash == std::string::npos ? std::string::npos
                                                                                                          : secondSlash - firstSlash - 1),
                                            uvs.size());
                        if (secondSlash != std::string::npos)
                            parsed.normal = resolve(value.substr(secondSlash + 1), normals.size());
                    }
                }
                catch (...)
                {
                    parsed.position = -1;
                }
                if (parsed.position >= 0)
                    face.push_back(parsed);
            }
            for (std::size_t index = 1; index + 1 < face.size(); ++index)
            {
                const FaceIndex triangle[3] = {face[0], face[index], face[index + 1]};
                const Vec3 a = positions[triangle[0].position], b = positions[triangle[1].position], c = positions[triangle[2].position];
                const Vec3 faceNormal = Normalize(Cross(b - a, c - a));
                const std::uint32_t base = static_cast<std::uint32_t>(m_vertices.size());
                const Vec3 trianglePositions[3] = {a, b, c};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const Vec3 normal = triangle[corner].normal >= 0 ? normals[triangle[corner].normal] : faceNormal;
                    const Vec2 uv =
                        triangle[corner].uv >= 0 ? uvs[triangle[corner].uv] : Vec2{corner == 1 ? 1.0f : 0.0f, corner == 2 ? 1.0f : 0.0f};
                    m_vertices.push_back({trianglePositions[corner], normal, uv});
                }
                m_indices.insert(m_indices.end(), {base, base + 1, base + 2});
            }
        }
    }
    if (m_vertices.empty())
    {
        SetState(ResourceState::Failed, "Mesh contains no valid faces: " + path);
        return false;
    }
    for (std::size_t triangle = 0; triangle + 2 < m_indices.size(); triangle += 3)
    {
        MeshVertex &first = m_vertices[m_indices[triangle]];
        MeshVertex &second = m_vertices[m_indices[triangle + 1]];
        MeshVertex &third = m_vertices[m_indices[triangle + 2]];
        const Vec3 edge1 = second.position - first.position, edge2 = third.position - first.position;
        const float du1 = second.uv.x - first.uv.x, dv1 = second.uv.y - first.uv.y;
        const float du2 = third.uv.x - first.uv.x, dv2 = third.uv.y - first.uv.y;
        const float determinant = du1 * dv2 - du2 * dv1;
        const Vec3 tangent = std::abs(determinant) > 1e-8f ? (edge1 * dv2 - edge2 * dv1) / determinant : Vec3{1.0f, 0.0f, 0.0f};
        for (MeshVertex *vertex : {&first, &second, &third})
        {
            const Vec3 orthogonalTangent = Normalize(tangent - vertex->normal * Dot(vertex->normal, tangent));
            vertex->tangent =
                LengthSquared(orthogonalTangent) > 1e-8f ? orthogonalTangent : Normalize(Cross({0.0f, 1.0f, 0.0f}, vertex->normal));
            if (LengthSquared(vertex->tangent) < 1e-8f)
                vertex->tangent = {1.0f, 0.0f, 0.0f};
        }
    }
    SetState(ResourceState::Loaded);
    return true;
}

void MeshResource::Unload()
{
    m_vertices.clear();
    m_indices.clear();
    SetState(ResourceState::Unloaded);
}
std::size_t MeshResource::GetByteSize() const
{
    return m_vertices.size() * sizeof(MeshVertex) + m_indices.size() * sizeof(std::uint32_t);
}
const std::vector<MeshVertex> &MeshResource::GetVertices() const
{
    return m_vertices;
}
const std::vector<std::uint32_t> &MeshResource::GetIndices() const
{
    return m_indices;
}
const Aabb &MeshResource::GetBounds() const
{
    return m_bounds;
}

bool ShaderResource::Load(const std::string &path)
{
    MarkLoading(path);
    m_source = PlatformFile::ReadAllText(path);
    if (m_source.empty())
    {
        SetState(ResourceState::Failed, "Shader file is empty or missing: " + path);
        return false;
    }
    SetState(ResourceState::Loaded);
    return true;
}
void ShaderResource::Unload()
{
    m_source.clear();
    SetState(ResourceState::Unloaded);
}
std::size_t ShaderResource::GetByteSize() const
{
    return m_source.size();
}
const std::string &ShaderResource::GetSource() const
{
    return m_source;
}

bool MaterialResource::Load(const std::string &path)
{
    MarkLoading(path);
    m_baseColor = {0.3f, 0.7f, 1.0f, 1.0f};
    m_metallic = 0.0f;
    m_roughness = 0.7f;
    m_emissive = {};
    m_transparent = false;
    m_shaderPath.clear();
    m_texturePath.clear();
    m_normalTexturePath.clear();
    m_metallicTexturePath.clear();
    m_roughnessTexturePath.clear();
    m_aoTexturePath.clear();
    m_emissiveTexturePath.clear();
    std::istringstream stream(PlatformFile::ReadAllText(path));
    if (!stream)
    {
        SetState(ResourceState::Failed, "Material file not found: " + path);
        return false;
    }
    std::string key;
    while (stream >> key)
    {
        if (key == "color")
            stream >> m_baseColor.x >> m_baseColor.y >> m_baseColor.z >> m_baseColor.w;
        else if (key == "metallic")
            stream >> m_metallic;
        else if (key == "roughness")
            stream >> m_roughness;
        else if (key == "emissive")
            stream >> m_emissive.x >> m_emissive.y >> m_emissive.z;
        else if (key == "shader")
            stream >> m_shaderPath;
        else if (key == "texture")
            stream >> m_texturePath;
        else if (key == "normalTexture")
            stream >> m_normalTexturePath;
        else if (key == "metallicTexture")
            stream >> m_metallicTexturePath;
        else if (key == "roughnessTexture")
            stream >> m_roughnessTexturePath;
        else if (key == "aoTexture")
            stream >> m_aoTexturePath;
        else if (key == "emissiveTexture")
            stream >> m_emissiveTexturePath;
        else if (key == "blend")
        {
            std::string value;
            stream >> value;
            m_transparent = value == "alpha" || value == "transparent";
        }
        else
        {
            std::string ignored;
            std::getline(stream, ignored);
        }
    }
    m_metallic = std::clamp(m_metallic, 0.0f, 1.0f);
    m_roughness = std::clamp(m_roughness, 0.04f, 1.0f);
    SetState(ResourceState::Loaded);
    return true;
}
void MaterialResource::Unload()
{
    m_shaderPath.clear();
    m_texturePath.clear();
    m_normalTexturePath.clear();
    m_metallicTexturePath.clear();
    m_roughnessTexturePath.clear();
    m_aoTexturePath.clear();
    m_emissiveTexturePath.clear();
    SetState(ResourceState::Unloaded);
}
Vec4 MaterialResource::GetBaseColor() const
{
    return m_baseColor;
}
const std::string &MaterialResource::GetShaderPath() const
{
    return m_shaderPath;
}
const std::string &MaterialResource::GetTexturePath() const
{
    return m_texturePath;
}
const std::string &MaterialResource::GetNormalTexturePath() const
{
    return m_normalTexturePath;
}
const std::string &MaterialResource::GetMetallicTexturePath() const
{
    return m_metallicTexturePath;
}
const std::string &MaterialResource::GetRoughnessTexturePath() const
{
    return m_roughnessTexturePath;
}
const std::string &MaterialResource::GetAoTexturePath() const
{
    return m_aoTexturePath;
}
const std::string &MaterialResource::GetEmissiveTexturePath() const
{
    return m_emissiveTexturePath;
}
float MaterialResource::GetMetallic() const
{
    return m_metallic;
}
float MaterialResource::GetRoughness() const
{
    return m_roughness;
}
Vec3 MaterialResource::GetEmissive() const
{
    return m_emissive;
}
bool MaterialResource::IsTransparent() const
{
    return m_transparent || m_baseColor.w < 0.999f;
}
} // namespace MiniEngine
