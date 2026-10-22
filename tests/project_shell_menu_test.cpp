#include "shell_menu.h"
#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}
std::wstring read(HKEY root, const std::wstring& path, const wchar_t* value)
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
    const std::filesystem::path first = L"C:\\SDK 中文\\programs\\ProjectTool\\TinyEngineProjectTool.exe";
    const std::filesystem::path second = L"C:\\Other SDK\\programs\\ProjectTool\\TinyEngineProjectTool.exe";
    bool ok = true;
    ok &= check(TinyEngineTools::registerProjectMenu(HKEY_CURRENT_USER, first, key).success, "first registration");
    const auto original = read(HKEY_CURRENT_USER, key + L"\\command", nullptr);
    ok &= check(original == L"\"" + first.wstring() + L"\" --generate-project \"%1\"", "quoted Unicode command");
    ok &= check(TinyEngineTools::registerProjectMenu(HKEY_CURRENT_USER, first, key).success, "idempotent register");
    ok &=
        check(TinyEngineTools::registerProjectMenu(HKEY_CURRENT_USER, second, key).success, "new SDK takes ownership");
    ok &= check(!TinyEngineTools::unregisterProjectMenu(HKEY_CURRENT_USER, first, key).success,
                "old SDK cannot unregister new SDK");
    ok &= check(read(HKEY_CURRENT_USER, key, L"TinyEngineOwner") == second.wstring(), "new owner retained");
    ok &=
        check(TinyEngineTools::unregisterProjectMenu(HKEY_CURRENT_USER, second, key).success, "unregister current SDK");
    ok &=
        check(TinyEngineTools::unregisterProjectMenu(HKEY_CURRENT_USER, second, key).success, "idempotent unregister");
    HKEY foreign = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &foreign, nullptr);
    const wchar_t foreignValue[] = L"Another application";
    RegSetValueExW(foreign, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(foreignValue), sizeof(foreignValue));
    RegCloseKey(foreign);
    ok &= check(!TinyEngineTools::registerProjectMenu(HKEY_CURRENT_USER, first, key).success, "refuse foreign menu");
    ok &= check(read(HKEY_CURRENT_USER, key, nullptr) == foreignValue, "foreign values unchanged");
    if (ok)
        std::cout << "PASS: isolated shell registration, quoting, updates and ownership\n";
    return ok ? 0 : 1;
}
