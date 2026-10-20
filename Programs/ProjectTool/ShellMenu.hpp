#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <string>

namespace TinyEngineTools
{
inline constexpr const wchar_t *ProjectMenuKey =
    L"Software\\Classes\\SystemFileAssociations\\.tinyproject\\shell\\TinyEngine.GenerateSolution";
struct MenuResult
{
    bool success = false;
    std::wstring message;
};
// The root and key are explicit so tests never touch the user's actual file association.
MenuResult RegisterProjectMenu(HKEY root, const std::filesystem::path &executable, const std::wstring &key = ProjectMenuKey);
MenuResult UnregisterProjectMenu(HKEY root, const std::filesystem::path &executable, const std::wstring &key = ProjectMenuKey);
} // namespace TinyEngineTools
