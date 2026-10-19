#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    DynamicLibrary(DynamicLibrary &&other) noexcept;
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;
    bool Open(const std::filesystem::path &, const std::vector<std::filesystem::path> &, std::string *error = nullptr);
    void *FindSymbol(const char *) const;
    void Close() noexcept;
private:
    void *m_handle = nullptr;
};
}
