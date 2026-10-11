#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace MiniEngine
{
std::string ResolveMaterialReference(const std::string &reference, const std::filesystem::path &owner, const std::filesystem::path &assetRoot);
std::vector<std::string> CollectMaterialReferences(const std::string &source, bool graph);
bool RewriteMaterialReferences(std::string &source, bool graph, const std::filesystem::path &owner,
                               const std::filesystem::path &newOwner, const std::filesystem::path &assetRoot,
                               const std::string &oldPath, const std::string &newPath);
} // namespace MiniEngine
