#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::uint64_t Hash(const std::string &text)
{
    std::uint64_t value = 1469598103934665603ull;
    for (const unsigned char character : text)
    {
        value ^= character;
        value *= 1099511628211ull;
    }
    return value;
}

bool Expand(const std::filesystem::path &path, std::set<std::string> &stack, std::vector<std::filesystem::path> &dependencies,
            std::string &output, std::string &error, const bool root)
{
    const auto normalized = path.lexically_normal();
    const std::string key = normalized.generic_string();
    if (!stack.insert(key).second)
    {
        error = "Cyclic shader include: " + key;
        return false;
    }
    const std::string text = PlatformFile::ReadAllText(normalized);
    if (text.empty())
    {
        error = "Shader source is empty or missing: " + key;
        stack.erase(key);
        return false;
    }
    dependencies.push_back(normalized);
    std::istringstream input(text);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const auto include = line.find("#include");
        if (include != std::string::npos)
        {
            const auto first = line.find('"', include), last = first == std::string::npos ? std::string::npos : line.find('"', first + 1);
            if (first == std::string::npos || last == std::string::npos)
            {
                error = key + ":" + std::to_string(lineNumber) + ": malformed #include";
                stack.erase(key);
                return false;
            }
            const auto includedPath = normalized.parent_path() / line.substr(first + 1, last - first - 1);
            output += "\n#line 1\n";
            if (!Expand(includedPath, stack, dependencies, output, error, false))
            {
                stack.erase(key);
                return false;
            }
            output += "\n#line " + std::to_string(lineNumber + 1) + "\n";
        }
        else if (line.find("#pragma variant") == std::string::npos && (!root || line.find("#version") == std::string::npos))
            output += line + '\n';
    }
    stack.erase(key);
    return true;
}
} // namespace

ShaderPreprocessResult ShaderPipeline::Preprocess(const std::filesystem::path &path,
                                                  const std::unordered_map<std::string, std::string> &defines)
{
    ShaderPreprocessResult result;
    result.defines = defines;
    const std::string root = PlatformFile::ReadAllText(path);
    if (root.empty())
    {
        result.error = "Shader source is empty or missing: " + path.generic_string();
        return result;
    }
    std::string version = "#version 330 core";
    std::istringstream rootStream(root);
    std::string line;
    while (std::getline(rootStream, line))
        if (line.find("#version") != std::string::npos)
        {
            version = line;
            break;
        }
    result.source = version + '\n';
    std::vector<std::pair<std::string, std::string>> ordered(defines.begin(), defines.end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto &define : ordered)
        result.source += "#define " + define.first + ' ' + define.second + '\n';
    std::set<std::string> stack;
    result.success = Expand(path, stack, result.dependencies, result.source, result.error, true);
    if (result.success)
        result.hash = Hash(result.source);
    return result;
}

std::vector<ShaderPreprocessResult> ShaderPipeline::BuildVariants(const std::filesystem::path &path, const std::size_t maximumVariants)
{
    std::vector<std::pair<std::string, std::vector<std::string>>> declarations;
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string line;
    while (std::getline(input, line))
    {
        const auto pragma = line.find("#pragma variant");
        if (pragma == std::string::npos)
            continue;
        std::istringstream values(line.substr(pragma + 15));
        std::string name, value;
        values >> name;
        std::vector<std::string> options;
        while (values >> value)
            options.push_back(value);
        if (!name.empty() && !options.empty())
            declarations.push_back({name, std::move(options)});
    }
    std::vector<std::unordered_map<std::string, std::string>> combinations(1);
    for (const auto &declaration : declarations)
    {
        std::vector<std::unordered_map<std::string, std::string>> expanded;
        for (const auto &combination : combinations)
            for (const auto &value : declaration.second)
            {
                auto candidate = combination;
                candidate[declaration.first] = value;
                expanded.push_back(std::move(candidate));
                if (expanded.size() >= maximumVariants)
                    break;
            }
        combinations = std::move(expanded);
        if (combinations.size() >= maximumVariants)
            break;
    }
    std::vector<ShaderPreprocessResult> results;
    if (combinations.empty())
        combinations.emplace_back();
    for (const auto &combination : combinations)
        results.push_back(Preprocess(path, combination));
    return results;
}

bool ShaderPipeline::WriteCache(const ShaderPreprocessResult &result, const std::filesystem::path &cacheDirectory)
{
    if (!result.success)
        return false;
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << result.hash << ".glsl";
    return PlatformFile::WriteAllText(cacheDirectory / name.str(), result.source);
}
} // namespace MiniEngine
