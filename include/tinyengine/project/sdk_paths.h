#pragma once
#include <filesystem>

namespace tinyengine
{
// Find an installed SDK by walking upward from an executable's directory.
std::filesystem::path findProjectSdk(std::filesystem::path directory);
} // namespace tinyengine
