#include "ShellMenu.hpp"
#include <iostream>
#include <string>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
std::wstring Read(HKEY root, const std::wstring &path, const wchar_t *value)
{
    wchar_t data[32768]{};
    DWORD bytes = sizeof(data);
    if (RegGetValueW(root, path.c_str(), value, RRF_RT_REG_SZ, nullptr, data, &bytes) != ERROR_SUCCESS)
        return {};
    return data;
}
} // namespace

int main()
{
    const std::wstring base = L"Software\\TinyEngineProjectToolTests\\" + std::to_wstring(GetCurrentProcessId());
    const auto key = base + L"\\shell\\TinyEngine.GenerateSolution";
    struct Cleanup
    {
        std::wstring key;
        ~Cleanup()
        {
            RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        }
    } cleanup{base};
    const std::filesystem::path first = L"C:\\SDK 中文\\Programs\\ProjectTool\\TinyEngineProjectTool.exe";
    const std::filesystem::path second = L"C:\\Other SDK\\Programs\\ProjectTool\\TinyEngineProjectTool.exe";
    bool ok = true;
    ok &= Check(TinyEngineTools::RegisterProjectMenu(HKEY_CURRENT_USER, first, key).success, "first registration");
    const auto original = Read(HKEY_CURRENT_USER, key + L"\\command", nullptr);
    ok &= Check(original == L"\"" + first.wstring() + L"\" --generate-project \"%1\"", "quoted Unicode command");
    ok &= Check(TinyEngineTools::RegisterProjectMenu(HKEY_CURRENT_USER, first, key).success, "idempotent register");
    ok &= Check(TinyEngineTools::RegisterProjectMenu(HKEY_CURRENT_USER, second, key).success, "new SDK takes ownership");
    ok &= Check(!TinyEngineTools::UnregisterProjectMenu(HKEY_CURRENT_USER, first, key).success, "old SDK cannot unregister new SDK");
    ok &= Check(Read(HKEY_CURRENT_USER, key, L"TinyEngineOwner") == second.wstring(), "new owner retained");
    ok &= Check(TinyEngineTools::UnregisterProjectMenu(HKEY_CURRENT_USER, second, key).success, "unregister current SDK");
    ok &= Check(TinyEngineTools::UnregisterProjectMenu(HKEY_CURRENT_USER, second, key).success, "idempotent unregister");
    HKEY foreign = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &foreign, nullptr);
    const wchar_t foreignValue[] = L"Another application";
    RegSetValueExW(foreign, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(foreignValue), sizeof(foreignValue));
    RegCloseKey(foreign);
    ok &= Check(!TinyEngineTools::RegisterProjectMenu(HKEY_CURRENT_USER, first, key).success, "refuse foreign menu");
    ok &= Check(Read(HKEY_CURRENT_USER, key, nullptr) == foreignValue, "foreign values unchanged");
    if (ok)
        std::cout << "PASS: isolated shell registration, quoting, updates and ownership\n";
    return ok ? 0 : 1;
}
