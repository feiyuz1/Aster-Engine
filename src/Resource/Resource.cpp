// 文件：src/Resource/Resource.cpp
// 作用：定义资源状态机以及纹理、网格、材质和着色器的运行时数据。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/Resource.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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
std::uint32_t ReadU32Le(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
        return 0;
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint64_t ReadU64Le(const std::vector<std::uint8_t> &bytes, const std::size_t offset)
{
    return static_cast<std::uint64_t>(ReadU32Le(bytes, offset)) | (static_cast<std::uint64_t>(ReadU32Le(bytes, offset + 4)) << 32u);
}

bool LoadDds(const std::vector<std::uint8_t> &bytes, int &width, int &height, TextureResource::Compression &compression, bool &srgb,
             std::vector<TextureResource::MipLevel> &levels, std::vector<std::uint8_t> &blocks)
{
    if (bytes.size() < 128 || ReadU32Le(bytes, 0) != 0x20534444u || ReadU32Le(bytes, 4) != 124u)
        return false;
    width = static_cast<int>(ReadU32Le(bytes, 16));
    height = static_cast<int>(ReadU32Le(bytes, 12));
    const std::uint32_t mipCount = std::max(1u, ReadU32Le(bytes, 28));
    const std::uint32_t fourCc = ReadU32Le(bytes, 84);
    std::size_t dataOffset = 128;
    srgb = false;
    if (fourCc == 0x31545844u)
        compression = TextureResource::Compression::BC1;
    else if (fourCc == 0x33545844u)
        compression = TextureResource::Compression::BC2;
    else if (fourCc == 0x35545844u)
        compression = TextureResource::Compression::BC3;
    else if (fourCc == 0x31495441u || fourCc == 0x55344342u)
        compression = TextureResource::Compression::BC4;
    else if (fourCc == 0x32495441u || fourCc == 0x55354342u)
        compression = TextureResource::Compression::BC5;
    else if (fourCc == 0x30315844u && bytes.size() >= 148)
    {
        dataOffset = 148;
        const std::uint32_t format = ReadU32Le(bytes, 128);
        srgb = format == 72 || format == 75 || format == 78 || format == 99;
        if (format == 70 || format == 71 || format == 72)
            compression = TextureResource::Compression::BC1;
        else if (format == 73 || format == 74 || format == 75)
            compression = TextureResource::Compression::BC2;
        else if (format == 76 || format == 77 || format == 78)
            compression = TextureResource::Compression::BC3;
        else if (format == 79 || format == 80 || format == 81)
            compression = TextureResource::Compression::BC4;
        else if (format == 82 || format == 83 || format == 84)
            compression = TextureResource::Compression::BC5;
        else if (format == 94 || format == 95 || format == 96)
            compression = TextureResource::Compression::BC6H;
        else if (format == 97 || format == 98 || format == 99)
            compression = TextureResource::Compression::BC7;
        else
            return false;
    }
    else
        return false;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        return false;
    const std::size_t blockBytes =
        compression == TextureResource::Compression::BC1 || compression == TextureResource::Compression::BC4 ? 8 : 16;
    std::size_t cursor = dataOffset;
    int mipWidth = width, mipHeight = height;
    for (std::uint32_t mip = 0; mip < mipCount; ++mip)
    {
        const std::size_t size = static_cast<std::size_t>((mipWidth + 3) / 4) * ((mipHeight + 3) / 4) * blockBytes;
        if (cursor + size > bytes.size())
            return false;
        levels.push_back({mipWidth, mipHeight, cursor - dataOffset, size});
        cursor += size;
        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }
    blocks.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset), bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
    return true;
}

bool LoadKtx2(const std::vector<std::uint8_t> &bytes, int &width, int &height, TextureResource::Compression &compression, bool &srgb,
              std::vector<TextureResource::MipLevel> &levels, std::vector<std::uint8_t> &blocks)
{
    static constexpr std::uint8_t signature[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 80 || !std::equal(std::begin(signature), std::end(signature), bytes.begin()))
        return false;
    const std::uint32_t format = ReadU32Le(bytes, 12), levelCount = std::max(1u, ReadU32Le(bytes, 40));
    width = static_cast<int>(ReadU32Le(bytes, 20));
    height = static_cast<int>(ReadU32Le(bytes, 24));
    if (ReadU32Le(bytes, 32) > 1 || ReadU32Le(bytes, 36) > 1 || ReadU32Le(bytes, 44) != 0 || width <= 0 || height <= 0 ||
        80ull + static_cast<std::uint64_t>(levelCount) * 24ull > bytes.size())
        return false;
    srgb = format == 132 || format == 134 || format == 136 || format == 138 || format == 146;
    if (format >= 131 && format <= 134)
        compression = TextureResource::Compression::BC1;
    else if (format == 135 || format == 136)
        compression = TextureResource::Compression::BC2;
    else if (format == 137 || format == 138)
        compression = TextureResource::Compression::BC3;
    else if (format == 139 || format == 140)
        compression = TextureResource::Compression::BC4;
    else if (format == 141 || format == 142)
        compression = TextureResource::Compression::BC5;
    else if (format == 143 || format == 144)
        compression = TextureResource::Compression::BC6H;
    else if (format == 145 || format == 146)
        compression = TextureResource::Compression::BC7;
    else
        return false;
    for (std::uint32_t mip = 0; mip < levelCount; ++mip)
    {
        const std::uint64_t offset = ReadU64Le(bytes, 80 + mip * 24), length = ReadU64Le(bytes, 88 + mip * 24);
        if (offset + length > bytes.size() || length > std::numeric_limits<std::size_t>::max())
            return false;
        const std::size_t destination = blocks.size();
        blocks.insert(blocks.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        levels.push_back({std::max(1, width >> mip), std::max(1, height >> mip), destination, static_cast<std::size_t>(length)});
    }
    return true;
}

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
} // 结束匿名命名空间

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

// 条件变量谓词同时接受 Loaded 与 Failed，保证失败资源不会让等待线程永久阻塞。
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

// 错误字符串和状态在同一临界区发布，再统一唤醒所有同步及异步调用方。
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
    m_compression = Compression::None;
    m_mipLevels.clear();
    m_srgbEncoded = false;
    const auto bytes = PlatformFile::ReadAllBytes(std::filesystem::u8path(path));
    std::string extension = std::filesystem::u8path(path).extension().generic_u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if ((extension == ".dds" && LoadDds(bytes, m_width, m_height, m_compression, m_srgbEncoded, m_mipLevels, m_data)) ||
        (extension == ".ktx2" && LoadKtx2(bytes, m_width, m_height, m_compression, m_srgbEncoded, m_mipLevels, m_data)))
    {
        SetState(ResourceState::Loaded);
        return true;
    }
    if (bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, "TNYTEX1\0"))
    {
        BinaryReader reader(bytes);
        char signature[8]{};
        std::uint32_t width = 0, height = 0;
        std::uint64_t valueCount = 0;
        bool hdr = false;
        if (!reader.ReadBytes(signature, sizeof(signature)) || !reader.ReadU32(width) || !reader.ReadU32(height) || !reader.ReadBool(hdr) ||
            !reader.ReadU64(valueCount) || width == 0 || height == 0 || width > 32768 || height > 32768 ||
            valueCount != static_cast<std::uint64_t>(width) * height * 4)
        {
            SetState(ResourceState::Failed, "Invalid compiled texture: " + path);
            return false;
        }
        m_width = static_cast<int>(width);
        m_height = static_cast<int>(height);
        if (hdr)
        {
            m_floatData.resize(static_cast<std::size_t>(valueCount));
            for (float &value : m_floatData)
                if (!reader.ReadFloat(value))
                {
                    SetState(ResourceState::Failed, "Truncated compiled HDR texture: " + path);
                    return false;
                }
        }
        else
        {
            m_data.resize(static_cast<std::size_t>(valueCount));
            if (!reader.ReadBytes(m_data.data(), m_data.size()))
            {
                SetState(ResourceState::Failed, "Truncated compiled texture: " + path);
                return false;
            }
        }
        SetState(ResourceState::Loaded);
        return true;
    }
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
    m_compression = Compression::None;
    m_mipLevels.clear();
    m_srgbEncoded = false;
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
TextureResource::Compression TextureResource::GetCompression() const
{
    return m_compression;
}
const std::vector<TextureResource::MipLevel> &TextureResource::GetMipLevels() const
{
    return m_mipLevels;
}
bool TextureResource::IsSrgbEncoded() const
{
    return m_srgbEncoded;
}

bool MeshResource::Load(const std::string &path)
{
    MarkLoading(path);
    const auto bytes = PlatformFile::ReadAllBytes(std::filesystem::u8path(path));
    const bool compiledMeshV1 = bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, "TNYMSH1\0");
    const bool compiledMeshV2 = bytes.size() >= 8 && std::equal(bytes.begin(), bytes.begin() + 8, "TNYMSH2\0");
    if (compiledMeshV1 || compiledMeshV2)
    {
        BinaryReader reader(bytes);
        char signature[8]{};
        std::uint64_t vertexCount = 0, indexCount = 0;
        if (!reader.ReadBytes(signature, sizeof(signature)) || !reader.ReadU64(vertexCount) || !reader.ReadU64(indexCount) ||
            vertexCount > 10'000'000 || indexCount > 30'000'000)
        {
            SetState(ResourceState::Failed, "Invalid compiled mesh: " + path);
            return false;
        }
        m_vertices.assign(static_cast<std::size_t>(vertexCount), {});
        m_indices.assign(static_cast<std::size_t>(indexCount), 0);
        m_bounds.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
        m_bounds.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (auto &vertex : m_vertices)
        {
            if (!reader.ReadFloat(vertex.position.x) || !reader.ReadFloat(vertex.position.y) || !reader.ReadFloat(vertex.position.z) ||
                !reader.ReadFloat(vertex.normal.x) || !reader.ReadFloat(vertex.normal.y) || !reader.ReadFloat(vertex.normal.z) ||
                !reader.ReadFloat(vertex.uv.x) || !reader.ReadFloat(vertex.uv.y) || !reader.ReadFloat(vertex.tangent.x) ||
                !reader.ReadFloat(vertex.tangent.y) || !reader.ReadFloat(vertex.tangent.z) ||
                (compiledMeshV2 && (!reader.ReadFloat(vertex.boneIndices.x) || !reader.ReadFloat(vertex.boneIndices.y) ||
                                    !reader.ReadFloat(vertex.boneIndices.z) || !reader.ReadFloat(vertex.boneIndices.w) ||
                                    !reader.ReadFloat(vertex.boneWeights.x) || !reader.ReadFloat(vertex.boneWeights.y) ||
                                    !reader.ReadFloat(vertex.boneWeights.z) || !reader.ReadFloat(vertex.boneWeights.w))))
            {
                SetState(ResourceState::Failed, "Truncated compiled mesh: " + path);
                return false;
            }
            m_bounds.minimum.x = std::min(m_bounds.minimum.x, vertex.position.x);
            m_bounds.minimum.y = std::min(m_bounds.minimum.y, vertex.position.y);
            m_bounds.minimum.z = std::min(m_bounds.minimum.z, vertex.position.z);
            m_bounds.maximum.x = std::max(m_bounds.maximum.x, vertex.position.x);
            m_bounds.maximum.y = std::max(m_bounds.maximum.y, vertex.position.y);
            m_bounds.maximum.z = std::max(m_bounds.maximum.z, vertex.position.z);
        }
        for (auto &index : m_indices)
            if (!reader.ReadU32(index) || index >= vertexCount)
            {
                SetState(ResourceState::Failed, "Invalid compiled mesh indices: " + path);
                return false;
            }
        SetState(ResourceState::Loaded);
        return true;
    }
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    if (!stream)
    {
        SetState(ResourceState::Failed, "Mesh file not found: " + path);
        return false;
    }
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> boneIndices;
    std::vector<Vec4> boneWeights;
    struct FaceIndex
    {
        int position = -1, uv = -1, normal = -1;
        bool operator==(const FaceIndex &other) const
        {
            return position == other.position && uv == other.uv && normal == other.normal;
        }
    };
    struct FaceIndexHash
    {
        std::size_t operator()(const FaceIndex &value) const
        {
            std::size_t hash = std::hash<int>{}(value.position);
            hash ^= std::hash<int>{}(value.uv) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            hash ^= std::hash<int>{}(value.normal) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };
    std::unordered_map<FaceIndex, std::uint32_t, FaceIndexHash> vertexLookup;
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
        else if (type == "vw")
        {
            int vertex = 0;
            Vec4 indices{}, weights{};
            if (row >> vertex >> indices.x >> weights.x >> indices.y >> weights.y >> indices.z >> weights.z >> indices.w >> weights.w)
            {
                if (boneIndices.size() < positions.size())
                {
                    boneIndices.resize(positions.size());
                    boneWeights.resize(positions.size());
                }
                if (vertex > 0 && static_cast<std::size_t>(vertex) <= positions.size())
                {
                    boneIndices[vertex - 1] = indices;
                    boneWeights[vertex - 1] = weights;
                }
            }
        }
        else if (type == "f")
        {
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
                const Vec3 trianglePositions[3] = {a, b, c};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const auto found = triangle[corner].normal >= 0 ? vertexLookup.find(triangle[corner]) : vertexLookup.end();
                    if (found != vertexLookup.end())
                    {
                        m_indices.push_back(found->second);
                        continue;
                    }
                    const Vec3 normal = triangle[corner].normal >= 0 ? normals[triangle[corner].normal] : faceNormal;
                    const Vec2 uv =
                        triangle[corner].uv >= 0 ? uvs[triangle[corner].uv] : Vec2{corner == 1 ? 1.0f : 0.0f, corner == 2 ? 1.0f : 0.0f};
                    MeshVertex vertex{trianglePositions[corner], normal, uv};
                    if (static_cast<std::size_t>(triangle[corner].position) < boneWeights.size())
                    {
                        vertex.boneIndices = boneIndices[triangle[corner].position];
                        vertex.boneWeights = boneWeights[triangle[corner].position];
                    }
                    const std::uint32_t vertexIndex = static_cast<std::uint32_t>(m_vertices.size());
                    m_vertices.push_back(vertex);
                    m_indices.push_back(vertexIndex);
                    if (triangle[corner].normal >= 0)
                        vertexLookup.emplace(triangle[corner], vertexIndex);
                }
            }
        }
    }
    // Assimp 可能合法导入不含三角面的曲线或点图元；保留其顶点以通过注册表校验和烘焙，
    // 三角形渲染器只会收到空索引缓冲。
    if (m_vertices.empty() && !positions.empty())
    {
        m_vertices.reserve(positions.size());
        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            MeshVertex vertex;
            vertex.position = positions[index];
            vertex.normal = index < normals.size() && LengthSquared(normals[index]) > 1e-8f ? Normalize(normals[index]) : Vec3{0.0f, 1.0f, 0.0f};
            vertex.uv = index < uvs.size() ? uvs[index] : Vec2{};
            vertex.tangent = {1.0f, 0.0f, 0.0f};
            if (index < boneIndices.size() && index < boneWeights.size())
            {
                vertex.boneIndices = boneIndices[index];
                vertex.boneWeights = boneWeights[index];
            }
            m_vertices.push_back(vertex);
        }
    }
    if (m_vertices.empty())
    {
        SetState(ResourceState::Failed, "Mesh contains no valid faces: " + path);
        return false;
    }
    std::vector<Vec3> tangentSums(m_vertices.size());
    for (std::size_t triangle = 0; triangle + 2 < m_indices.size(); triangle += 3)
    {
        const MeshVertex &first = m_vertices[m_indices[triangle]];
        const MeshVertex &second = m_vertices[m_indices[triangle + 1]];
        const MeshVertex &third = m_vertices[m_indices[triangle + 2]];
        const Vec3 edge1 = second.position - first.position, edge2 = third.position - first.position;
        const float du1 = second.uv.x - first.uv.x, dv1 = second.uv.y - first.uv.y;
        const float du2 = third.uv.x - first.uv.x, dv2 = third.uv.y - first.uv.y;
        const float determinant = du1 * dv2 - du2 * dv1;
        const Vec3 tangent = std::abs(determinant) > 1e-8f ? (edge1 * dv2 - edge2 * dv1) / determinant : Vec3{1.0f, 0.0f, 0.0f};
        tangentSums[m_indices[triangle]] += tangent;
        tangentSums[m_indices[triangle + 1]] += tangent;
        tangentSums[m_indices[triangle + 2]] += tangent;
    }
    for (std::size_t index = 0; index < m_vertices.size(); ++index)
    {
        MeshVertex &vertex = m_vertices[index];
        const Vec3 orthogonalTangent = Normalize(tangentSums[index] - vertex.normal * Dot(vertex.normal, tangentSums[index]));
        vertex.tangent =
            LengthSquared(orthogonalTangent) > 1e-8f ? orthogonalTangent : Normalize(Cross({0.0f, 1.0f, 0.0f}, vertex.normal));
        if (LengthSquared(vertex.tangent) < 1e-8f)
            vertex.tangent = {1.0f, 0.0f, 0.0f};
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
    return LoadWithResolver(path, {});
}
bool MaterialResource::LoadWithResolver(const std::string &path, ReferenceResolver resolver, std::size_t maxParentDepth, SourceReader reader)
{
    const bool hadValidData = IsReady();
    MaterialResource candidate;
    candidate.m_referenceResolver = std::move(resolver);
    candidate.m_maxParentDepth = maxParentDepth;
    candidate.m_sourceReader=std::move(reader);
    if (!candidate.LoadCandidate(path))
    {
        SetState(hadValidData ? ResourceState::Loaded : ResourceState::Failed, candidate.GetError());
        return false;
    }
    MarkLoading(path);
    m_baseColor = candidate.m_baseColor;
    m_metallic = candidate.m_metallic;
    m_roughness = candidate.m_roughness;
    m_emissive = candidate.m_emissive;
    m_transparent = candidate.m_transparent;
    m_shaderPath = std::move(candidate.m_shaderPath);
    m_texturePath = std::move(candidate.m_texturePath);
    m_normalTexturePath = std::move(candidate.m_normalTexturePath);
    m_metallicTexturePath = std::move(candidate.m_metallicTexturePath);
    m_roughnessTexturePath = std::move(candidate.m_roughnessTexturePath);
    m_aoTexturePath = std::move(candidate.m_aoTexturePath);
    m_emissiveTexturePath = std::move(candidate.m_emissiveTexturePath);
    m_parentPath = std::move(candidate.m_parentPath);
    m_overrides = std::move(candidate.m_overrides);
    m_parameters = std::move(candidate.m_parameters);
    m_packedTexturePath = std::move(candidate.m_packedTexturePath);
    m_packedTextureLayout = std::move(candidate.m_packedTextureLayout);
    m_materialData = std::move(candidate.m_materialData);
    SetState(ResourceState::Loaded);
    return true;
}
bool MaterialResource::Save(const std::string &path) const
{
    if (!IsReady()) return false;
    const auto source = SerializeMaterialAsset(m_materialData);
    if (m_materialData.formatVersion == 2)
    {
        MaterialAssetData validated;
        std::string error;
        if (!ParseMaterialAsset(source, validated, error)) return false;
    }
    return PlatformFile::WriteAllText(std::filesystem::u8path(path), source);
}
void MaterialResource::CaptureLegacyData(const std::string &source)
{
    m_materialData = {};
    m_materialData.parentPath = m_parentPath;
    auto &data = m_materialData.legacyData;
    data.baseColor = m_baseColor;
    data.metallic = m_metallic;
    data.roughness = m_roughness;
    data.emissive = m_emissive;
    data.transparent = m_transparent;
    data.shaderPath = m_shaderPath;
    data.texturePath = m_texturePath;
    data.normalTexturePath = m_normalTexturePath;
    data.metallicTexturePath = m_metallicTexturePath;
    data.roughnessTexturePath = m_roughnessTexturePath;
    data.aoTexturePath = m_aoTexturePath;
    data.emissiveTexturePath = m_emissiveTexturePath;
    data.packedTexturePath = m_packedTexturePath;
    data.packedTextureLayout = m_packedTextureLayout;
    data.overrideFlags.insert(m_overrides.begin(), m_overrides.end());
    data.parameters.insert(m_parameters.begin(), m_parameters.end());
    data.source = source;
}
bool MaterialResource::LoadCandidate(const std::string &path)
{
    MarkLoading(path);
    static thread_local std::unordered_set<std::string> loadingMaterials;
    if (loadingMaterials.size() >= m_maxParentDepth)
    { SetState(ResourceState::Failed, "Material parent chain exceeds 32 instances: " + path); return false; }
    const std::string normalizedPath = PlatformFile::Normalize(std::filesystem::u8path(path)).generic_u8string();
    if (!loadingMaterials.insert(normalizedPath).second)
    {
        SetState(ResourceState::Failed, "Cyclic material parent: " + path);
        return false;
    }
    struct LoadingGuard
    {
        std::unordered_set<std::string> &paths;
        std::string path;
        ~LoadingGuard()
        {
            paths.erase(path);
        }
    } guard{loadingMaterials, normalizedPath};
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
    m_parentPath.clear();
    m_overrides.clear();
    m_parameters.clear();
    m_packedTexturePath.clear();
    m_packedTextureLayout = "ORM";
    const std::string source = m_sourceReader ? m_sourceReader(path) : PlatformFile::ReadAllText(std::filesystem::u8path(path));
    if (source.empty())
    {
        SetState(ResourceState::Failed, "Material file not found: " + path);
        return false;
    }
    std::istringstream headerStream(source);
    std::string header;
    headerStream >> header;
    if (header == "TINY_MAT")
    {
        std::string error;
        if (!ParseMaterialAsset(source, m_materialData, error))
        {
            SetState(ResourceState::Failed, error);
            return false;
        }
        m_parentPath = m_materialData.parentPath;
        SetState(ResourceState::Loaded);
        return true;
    }
    {
        std::istringstream parentStream(source);
        std::string parentKey;
        while (parentStream >> parentKey)
            if (parentKey == "parent")
            {
                parentStream >> std::quoted(m_parentPath);
                break;
            }
            else
            {
                std::string ignored;
                std::getline(parentStream, ignored);
            }
    }
    {
        std::istringstream overrideStream(source);
        std::string token;
        while (overrideStream >> token)
            if (token == "override")
            {
                std::string property;
                bool enabled = false;
                if (overrideStream >> property >> enabled)
                    m_overrides[property] = enabled;
            }
            else
            {
                std::string ignored;
                std::getline(overrideStream, ignored);
            }
    }
    if (!m_parentPath.empty())
    {
        std::filesystem::path parent = std::filesystem::u8path(m_parentPath);
        if (m_referenceResolver)
            parent = std::filesystem::u8path(m_referenceResolver(m_parentPath, path));
        else if (!PlatformFile::Exists(parent))
            parent = std::filesystem::u8path(path).parent_path() / parent;
        MaterialResource base;
        if (!base.LoadWithResolver(parent.generic_u8string(), m_referenceResolver, m_maxParentDepth,m_sourceReader))
        {
            SetState(ResourceState::Failed, "Cannot load material parent: " + m_parentPath + " -> " + base.GetError());
            return false;
        }
        m_baseColor = base.m_baseColor;
        m_shaderPath = base.m_shaderPath;
        m_texturePath = base.m_texturePath;
        m_normalTexturePath = base.m_normalTexturePath;
        m_metallicTexturePath = base.m_metallicTexturePath;
        m_roughnessTexturePath = base.m_roughnessTexturePath;
        m_aoTexturePath = base.m_aoTexturePath;
        m_emissiveTexturePath = base.m_emissiveTexturePath;
        m_metallic = base.m_metallic;
        m_roughness = base.m_roughness;
        m_emissive = base.m_emissive;
        m_transparent = base.m_transparent;
        m_parameters = base.m_parameters;
        m_packedTexturePath = base.m_packedTexturePath;
        m_packedTextureLayout = base.m_packedTextureLayout;
    }
    const auto applies = [this](const std::string &property) {
        const auto found = m_overrides.find(property);
        return m_parentPath.empty() || found == m_overrides.end() || found->second;
    };
    std::istringstream stream(source);
    std::string key;
    while (stream >> key)
    {
        if (key == "parent")
        {
            std::string ignored;
            stream >> std::quoted(ignored);
        }
        else if (key == "override")
        {
            std::string property;
            bool ignored = false;
            stream >> property >> ignored;
        }
        else if (key == "color")
        {
            Vec4 value;
            stream >> value.x >> value.y >> value.z >> value.w;
            if (applies(key))
                m_baseColor = value;
        }
        else if (key == "metallic" || key == "roughness")
        {
            float value = 0.0f;
            stream >> value;
            if (applies(key))
                (key == "metallic" ? m_metallic : m_roughness) = value;
        }
        else if (key == "emissive")
        {
            Vec3 value;
            stream >> value.x >> value.y >> value.z;
            if (applies(key))
                m_emissive = value;
        }
        else if (key == "shader" || key == "texture" || key == "normalTexture" || key == "metallicTexture" || key == "roughnessTexture" ||
                 key == "aoTexture" || key == "emissiveTexture" || key == "packedTexture")
        {
            std::string value;
            stream >> std::quoted(value);
            if (applies(key))
            {
                if (key == "shader")
                    m_shaderPath = value;
                else if (key == "texture")
                    m_texturePath = value;
                else if (key == "normalTexture")
                    m_normalTexturePath = value;
                else if (key == "metallicTexture")
                    m_metallicTexturePath = value;
                else if (key == "roughnessTexture")
                    m_roughnessTexturePath = value;
                else if (key == "aoTexture")
                    m_aoTexturePath = value;
                else if (key == "emissiveTexture")
                    m_emissiveTexturePath = value;
                else
                    m_packedTexturePath = value;
            }
        }
        else if (key == "packedLayout")
        {
            std::string value;
            stream >> value;
            if (applies(key) && (value == "ORM" || value == "RMA"))
                m_packedTextureLayout = value;
        }
        else if (key == "parameter")
        {
            std::string name;
            Vec4 value;
            stream >> name >> value.x >> value.y >> value.z >> value.w;
            if (applies("parameter." + name))
                m_parameters[name] = value;
        }
        else if (key == "blend")
        {
            std::string value;
            stream >> value;
            if (applies(key))
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
    if (m_referenceResolver)
        for (auto *reference : {&m_shaderPath, &m_texturePath, &m_normalTexturePath, &m_metallicTexturePath, &m_roughnessTexturePath,
                                &m_aoTexturePath, &m_emissiveTexturePath, &m_packedTexturePath})
            if (!reference->empty()) *reference = m_referenceResolver(*reference, path);
    CaptureLegacyData(source);
    SetState(ResourceState::Loaded);
    return true;
}
void MaterialResource::Unload()
{
    m_materialData = {};
    m_shaderPath.clear();
    m_texturePath.clear();
    m_normalTexturePath.clear();
    m_metallicTexturePath.clear();
    m_roughnessTexturePath.clear();
    m_aoTexturePath.clear();
    m_emissiveTexturePath.clear();
    m_parentPath.clear();
    m_overrides.clear();
    m_parameters.clear();
    m_packedTexturePath.clear();
    SetState(ResourceState::Unloaded);
}

std::size_t MaterialResource::GetByteSize() const
{
    return sizeof(*this) + m_path.size() + m_shaderPath.size() + m_texturePath.size() + m_normalTexturePath.size() +
           m_metallicTexturePath.size() + m_roughnessTexturePath.size() + m_aoTexturePath.size() + m_emissiveTexturePath.size() +
           m_parentPath.size() + m_packedTexturePath.size() + m_packedTextureLayout.size() +
           m_overrides.size() * (sizeof(bool) + sizeof(std::string)) + m_parameters.size() * (sizeof(Vec4) + sizeof(std::string));
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
const std::string &MaterialResource::GetParentPath() const
{
    return m_parentPath;
}
bool MaterialResource::IsOverridden(const std::string &property) const
{
    const auto found = m_overrides.find(property);
    return found == m_overrides.end() ? m_parentPath.empty() : found->second;
}
const std::unordered_map<std::string, Vec4> &MaterialResource::GetParameters() const
{
    return m_parameters;
}
const std::string &MaterialResource::GetPackedTexturePath() const
{
    return m_packedTexturePath;
}
const std::string &MaterialResource::GetPackedTextureLayout() const
{
    return m_packedTextureLayout;
}
} // 结束 MiniEngine 命名空间
