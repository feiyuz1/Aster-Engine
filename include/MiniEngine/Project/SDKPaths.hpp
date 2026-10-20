#pragma once
#include <filesystem>

namespace MiniEngine
{
// Find an installed SDK by walking upward from an executable's directory.
std::filesystem::path FindProjectSDK(std::filesystem::path directory);
} // namespace MiniEngine
