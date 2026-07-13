#include "MiniEngine/Resource/Resource.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cfloat>
#include <sstream>

namespace MiniEngine
{
    const std::string& Resource::GetPath() const { return m_path; }
    ResourceState Resource::GetState() const { return m_state.load(); }
    bool Resource::IsReady() const { return GetState() == ResourceState::Loaded; }

    std::string Resource::GetError() const
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_error;
    }

    bool Resource::Wait() const
    {
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_stateCv.wait(lock, [this]() { const auto state = m_state.load(); return state != ResourceState::Loading; });
        return m_state.load() == ResourceState::Loaded;
    }

    void Resource::MarkLoading(const std::string& path)
    {
        if (!path.empty()) m_path = path;
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

    bool TextureResource::Load(const std::string& path)
    {
        MarkLoading(path);
        std::istringstream stream(PlatformFile::ReadAllText(path));
        std::string magic;
        stream >> magic;
        if (magic != "P3")
        {
            SetState(ResourceState::Failed, "Only ASCII PPM (P3) textures are supported: " + path);
            return false;
        }
        auto readValue = [&stream]()
        {
            std::string token;
            while (stream >> token)
            {
                if (!token.empty() && token.front() == '#') { std::string ignored; std::getline(stream, ignored); continue; }
                return std::stoi(token);
            }
            return -1;
        };
        m_width = readValue(); m_height = readValue(); const int maximum = readValue();
        if (m_width <= 0 || m_height <= 0 || maximum <= 0) { SetState(ResourceState::Failed, "Invalid PPM header: " + path); return false; }
        m_data.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4);
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height); ++pixel)
        {
            const int red = readValue(), green = readValue(), blue = readValue();
            if (red < 0 || green < 0 || blue < 0) { m_data.clear(); SetState(ResourceState::Failed, "Incomplete PPM pixels: " + path); return false; }
            m_data[pixel * 4] = static_cast<std::uint8_t>(std::clamp(red * 255 / maximum, 0, 255));
            m_data[pixel * 4 + 1] = static_cast<std::uint8_t>(std::clamp(green * 255 / maximum, 0, 255));
            m_data[pixel * 4 + 2] = static_cast<std::uint8_t>(std::clamp(blue * 255 / maximum, 0, 255));
            m_data[pixel * 4 + 3] = 255;
        }
        SetState(ResourceState::Loaded);
        return true;
    }

    void TextureResource::Unload() { m_data.clear(); m_width = m_height = 0; SetState(ResourceState::Unloaded); }
    std::size_t TextureResource::GetByteSize() const { return m_data.size(); }
    const std::vector<std::uint8_t>& TextureResource::GetData() const { return m_data; }
    int TextureResource::GetWidth() const { return m_width; }
    int TextureResource::GetHeight() const { return m_height; }

    bool MeshResource::Load(const std::string& path)
    {
        MarkLoading(path);
        std::istringstream stream(PlatformFile::ReadAllText(path));
        if (!stream)
        {
            SetState(ResourceState::Failed, "Mesh file not found: " + path);
            return false;
        }
        std::vector<Vec3> positions;
        std::string line;
        m_vertices.clear(); m_indices.clear();
        m_bounds.minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
        m_bounds.maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        while (std::getline(stream, line))
        {
            std::istringstream row(line);
            std::string type; row >> type;
            if (type == "v")
            {
                Vec3 value; if (!(row >> value.x >> value.y >> value.z)) continue;
                positions.push_back(value);
                m_bounds.minimum.x = std::min(m_bounds.minimum.x, value.x); m_bounds.minimum.y = std::min(m_bounds.minimum.y, value.y); m_bounds.minimum.z = std::min(m_bounds.minimum.z, value.z);
                m_bounds.maximum.x = std::max(m_bounds.maximum.x, value.x); m_bounds.maximum.y = std::max(m_bounds.maximum.y, value.y); m_bounds.maximum.z = std::max(m_bounds.maximum.z, value.z);
            }
            else if (type == "f")
            {
                std::vector<std::uint32_t> face;
                std::string value;
                while (row >> value)
                {
                    const auto slash = value.find('/');
                    const int index = std::stoi(value.substr(0, slash));
                    if (index > 0 && static_cast<std::size_t>(index) <= positions.size()) face.push_back(static_cast<std::uint32_t>(index - 1));
                }
                for (std::size_t index = 1; index + 1 < face.size(); ++index)
                {
                    const Vec3 a = positions[face[0]], b = positions[face[index]], c = positions[face[index + 1]];
                    const Vec3 normal = Normalize(Cross(b - a, c - a));
                    const std::uint32_t base = static_cast<std::uint32_t>(m_vertices.size());
                    m_vertices.push_back({a, normal, {0.0f, 0.0f}});
                    m_vertices.push_back({b, normal, {1.0f, 0.0f}});
                    m_vertices.push_back({c, normal, {0.0f, 1.0f}});
                    m_indices.insert(m_indices.end(), {base, base + 1, base + 2});
                }
            }
        }
        if (m_vertices.empty())
        {
            SetState(ResourceState::Failed, "Mesh contains no valid faces: " + path);
            return false;
        }
        SetState(ResourceState::Loaded);
        return true;
    }

    void MeshResource::Unload() { m_vertices.clear(); m_indices.clear(); SetState(ResourceState::Unloaded); }
    std::size_t MeshResource::GetByteSize() const { return m_vertices.size() * sizeof(MeshVertex) + m_indices.size() * sizeof(std::uint32_t); }
    const std::vector<MeshVertex>& MeshResource::GetVertices() const { return m_vertices; }
    const std::vector<std::uint32_t>& MeshResource::GetIndices() const { return m_indices; }
    const Aabb& MeshResource::GetBounds() const { return m_bounds; }

    bool ShaderResource::Load(const std::string& path)
    {
        MarkLoading(path); m_source = PlatformFile::ReadAllText(path);
        if (m_source.empty()) { SetState(ResourceState::Failed, "Shader file is empty or missing: " + path); return false; }
        SetState(ResourceState::Loaded); return true;
    }
    void ShaderResource::Unload() { m_source.clear(); SetState(ResourceState::Unloaded); }
    std::size_t ShaderResource::GetByteSize() const { return m_source.size(); }
    const std::string& ShaderResource::GetSource() const { return m_source; }

    bool MaterialResource::Load(const std::string& path)
    {
        MarkLoading(path);
        std::istringstream stream(PlatformFile::ReadAllText(path));
        if (!stream) { SetState(ResourceState::Failed, "Material file not found: " + path); return false; }
        std::string key;
        while (stream >> key)
        {
            if (key == "color") stream >> m_baseColor.x >> m_baseColor.y >> m_baseColor.z >> m_baseColor.w;
            else if (key == "shader") stream >> m_shaderPath;
            else if (key == "texture") stream >> m_texturePath;
            else if (key == "blend") { std::string value; stream >> value; m_transparent = value == "alpha" || value == "transparent"; }
            else { std::string ignored; std::getline(stream, ignored); }
        }
        SetState(ResourceState::Loaded); return true;
    }
    void MaterialResource::Unload() { m_shaderPath.clear(); m_texturePath.clear(); SetState(ResourceState::Unloaded); }
    Vec4 MaterialResource::GetBaseColor() const { return m_baseColor; }
    const std::string& MaterialResource::GetShaderPath() const { return m_shaderPath; }
    const std::string& MaterialResource::GetTexturePath() const { return m_texturePath; }
    bool MaterialResource::IsTransparent() const { return m_transparent || m_baseColor.w < 0.999f; }
}
