#include "MiniEngine/Resource/Resource.hpp"

#include <fstream>

namespace MiniEngine
{
    namespace
    {
        bool LoadFileBytes(const std::string& path, std::vector<std::uint8_t>& outBytes)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }

            const std::streamsize size = file.tellg();
            if (size < 0)
            {
                return false;
            }

            outBytes.resize(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);

            if (size > 0 && !file.read(reinterpret_cast<char*>(outBytes.data()), size))
            {
                return false;
            }

            return true;
        }
    }

    const std::string& Resource::GetPath() const
    {
        return m_path;
    }

    ResourceState Resource::GetState() const
    {
        return m_state;
    }

    void Resource::MarkLoading()
    {
        m_state = ResourceState::Loading;
    }

    void Resource::SetState(const ResourceState state)
    {
        m_state = state;
    }

    bool TextureResource::Load(const std::string& path)
    {
        m_path = path;
        SetState(ResourceState::Loading);

        if (!LoadFileBytes(path, m_data))
        {
            SetState(ResourceState::Failed);
            return false;
        }

        SetState(ResourceState::Loaded);
        return true;
    }

    void TextureResource::Unload()
    {
        m_data.clear();
        SetState(ResourceState::Unloaded);
    }

    std::size_t TextureResource::GetByteSize() const
    {
        return m_data.size();
    }

    bool MeshResource::Load(const std::string& path)
    {
        m_path = path;
        SetState(ResourceState::Loading);

        if (!LoadFileBytes(path, m_data))
        {
            SetState(ResourceState::Failed);
            return false;
        }

        SetState(ResourceState::Loaded);
        return true;
    }

    void MeshResource::Unload()
    {
        m_data.clear();
        SetState(ResourceState::Unloaded);
    }

    std::size_t MeshResource::GetByteSize() const
    {
        return m_data.size();
    }

    bool ShaderResource::Load(const std::string& path)
    {
        m_path = path;
        SetState(ResourceState::Loading);

        if (!LoadFileBytes(path, m_data))
        {
            SetState(ResourceState::Failed);
            return false;
        }

        SetState(ResourceState::Loaded);
        return true;
    }

    void ShaderResource::Unload()
    {
        m_data.clear();
        SetState(ResourceState::Unloaded);
    }

    std::size_t ShaderResource::GetByteSize() const
    {
        return m_data.size();
    }
}
