#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <string>

namespace TinyEngineTools
{
inline constexpr const wchar_t* kProjectMenuKey =
    L"Software\\Classes\\SystemFileAssociations\\.tinyproject\\shell\\TinyEngine.GenerateSolution";
struct MenuResult
{
    bool success = false;
    std::wstring message;
};
// The root and key are explicit so tests never touch the user's actual file association.
MenuResult registerProjectMenu(HKEY root, const std::filesystem::path& executable,
                               const std::wstring& key = kProjectMenuKey);
MenuResult unregisterProjectMenu(HKEY root, const std::filesystem::path& executable,
                                 const std::wstring& key = kProjectMenuKey);
} // namespace TinyEngineTools
