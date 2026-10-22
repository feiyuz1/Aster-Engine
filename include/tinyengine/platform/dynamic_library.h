#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace tinyengine
{
enum class DynamicLibrarySearch
{
    ExplicitDirectories,
    PlatformDefault
};
class DynamicLibrary
{
  public:
    DynamicLibrary() = default;
    explicit DynamicLibrary(DynamicLibrarySearch search) : m_search(search) {}
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    bool Open(const std::filesystem::path&, const std::vector<std::filesystem::path>&, std::string* error = nullptr);
    void* findSymbol(const char*) const;
    void Close() noexcept;

  private:
    void* m_handle = nullptr;
    DynamicLibrarySearch m_search = DynamicLibrarySearch::ExplicitDirectories;
};
} // namespace tinyengine
