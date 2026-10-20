#include "ShellMenu.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <vector>

namespace TinyEngineTools
{
namespace
{
struct Key
{
    HKEY value = nullptr;
    ~Key()
    {
        if (value)
            RegCloseKey(value);
    }
};
struct Value
{
    HKEY key;
    const wchar_t *name;
    bool exists = false;
    DWORD type = REG_SZ;
    std::vector<BYTE> data;
    LSTATUS Read()
    {
        DWORD size = 0;
        auto status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
        if (status == ERROR_FILE_NOT_FOUND)
            return ERROR_SUCCESS;
        if (status != ERROR_SUCCESS)
            return status;
        exists = true;
        data.resize(size);
        return RegQueryValueExW(key, name, nullptr, &type, data.data(), &size);
    }
    LSTATUS Restore() const
    {
        if (exists)
            return RegSetValueExW(key, name, 0, type, data.data(), static_cast<DWORD>(data.size()));
        const auto status = RegDeleteValueW(key, name);
        return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
    }
};
std::wstring StringValue(HKEY key, const wchar_t *name)
{
    wchar_t buffer[32768]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};
    return buffer;
}
LSTATUS Set(HKEY key, const wchar_t *name, const std::wstring &text)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(text.c_str()),
                          static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
}
MenuResult Failure(LSTATUS code)
{
    return {false, L"右键菜单操作失败，Windows 错误码：" + std::to_wstring(code)};
}
} // namespace

MenuResult RegisterProjectMenu(HKEY root, const std::filesystem::path &executable, const std::wstring &path)
{
    Key key;
    DWORD created = 0;
    auto status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key.value, &created);
    if (status != ERROR_SUCCESS)
        return Failure(status);
    const bool isNew = created == REG_CREATED_NEW_KEY;
    if (!isNew && StringValue(key.value, L"TinyEngineTool") != L"TinyEngineProjectTool")
        return {false, L"菜单键属于其他程序，已保留原设置。"};
    Key command;
    status = RegCreateKeyExW(key.value, L"command", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &command.value, nullptr);
    if (status != ERROR_SUCCESS)
    {
        if (isNew)
            RegDeleteTreeW(root, path.c_str());
        return Failure(status);
    }
    std::vector<Value> previous{{key.value, nullptr},           {key.value, L"Icon"},
                                {key.value, L"TinyEngineTool"}, {key.value, L"TinyEngineOwner"},
                                {key.value, L"LegacyDisable"},  {command.value, nullptr}};
    for (auto &value : previous)
    {
        status = value.Read();
        if (status != ERROR_SUCCESS)
        {
            if (isNew)
                RegDeleteTreeW(root, path.c_str());
            return Failure(status);
        }
    }
    const auto owner = std::filesystem::absolute(executable).lexically_normal().wstring();
    const std::wstring invocation = L"\"" + owner + L"\" --generate-project \"%1\"";
    status = Set(key.value, L"LegacyDisable", L"");
    if (status == ERROR_SUCCESS)
        status = Set(key.value, nullptr, L"生成 Visual Studio 工程");
    if (status == ERROR_SUCCESS)
        status = Set(key.value, L"Icon", L"\"" + owner + L"\",0");
    if (status == ERROR_SUCCESS)
        status = Set(key.value, L"TinyEngineTool", L"TinyEngineProjectTool");
    if (status == ERROR_SUCCESS)
        status = Set(key.value, L"TinyEngineOwner", owner);
    if (status == ERROR_SUCCESS)
        status = Set(command.value, nullptr, invocation);
    if (status == ERROR_SUCCESS)
        status = RegDeleteValueW(key.value, L"LegacyDisable");
    if (status != ERROR_SUCCESS)
    {
        LSTATUS restored = ERROR_SUCCESS;
        for (auto it = previous.rbegin(); it != previous.rend(); ++it)
        {
            const auto result = it->Restore();
            if (result != ERROR_SUCCESS)
                restored = result;
        }
        if (isNew)
            RegDeleteTreeW(root, path.c_str());
        auto result = Failure(status);
        if (restored != ERROR_SUCCESS)
            result.message += L"；恢复旧设置失败：" + std::to_wstring(restored);
        return result;
    }
    if (path == ProjectMenuKey)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {true, L"已注册当前用户的 .tinyproject 右键菜单。Windows 11 请在“显示更多选项”中查看。"};
}

MenuResult UnregisterProjectMenu(HKEY root, const std::filesystem::path &executable, const std::wstring &path)
{
    Key key;
    auto status = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key.value);
    if (status == ERROR_FILE_NOT_FOUND)
        return {true, L"右键菜单已移除。"};
    if (status != ERROR_SUCCESS)
        return Failure(status);
    const auto owner = std::filesystem::absolute(executable).lexically_normal().wstring();
    if (StringValue(key.value, L"TinyEngineTool") != L"TinyEngineProjectTool" ||
        _wcsicmp(StringValue(key.value, L"TinyEngineOwner").c_str(), owner.c_str()) != 0)
        return {false, L"菜单属于另一个安装位置，已保留其设置。"};
    status = RegDeleteTreeW(root, path.c_str());
    if (status != ERROR_SUCCESS)
        return Failure(status);
    if (path == ProjectMenuKey)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {true, L"已移除当前安装的 .tinyproject 右键菜单。"};
}
} // namespace TinyEngineTools
