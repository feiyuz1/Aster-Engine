#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MiniEngine
{
    enum class ResourceState
    {
        Unloaded,
        Loading,
        Loaded,
        Failed,
        PendingUnload
    };

    class Resource
    {
    public:
        virtual ~Resource() = default;

        const std::string& GetPath() const;
        ResourceState GetState() const;

        virtual bool Load(const std::string& path) = 0;
        virtual void Unload() = 0;

        void MarkLoading();

    protected:
        void SetState(ResourceState state);
        std::string m_path;
        ResourceState m_state = ResourceState::Unloaded;
    };

    class TextureResource final : public Resource
    {
    public:
        bool Load(const std::string& path) override;
        void Unload() override;

        std::size_t GetByteSize() const;

    private:
        std::vector<std::uint8_t> m_data;
    };

    class MeshResource final : public Resource
    {
    public:
        bool Load(const std::string& path) override;
        void Unload() override;

        std::size_t GetByteSize() const;

    private:
        std::vector<std::uint8_t> m_data;
    };

    class ShaderResource final : public Resource
    {
    public:
        bool Load(const std::string& path) override;
        void Unload() override;

        std::size_t GetByteSize() const;

    private:
        std::vector<std::uint8_t> m_data;
    };
}
