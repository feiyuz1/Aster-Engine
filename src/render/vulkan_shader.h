#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace tinyengine::vk
{
struct UniformValue
{
    std::vector<std::uint8_t> data;
    std::uint32_t elementSize = 0, count = 1;
};
using UniformValues = std::unordered_map<std::string, UniformValue>;
struct UniformField
{
    std::string name;
    std::uint32_t offset = 0, size = 0, arrayStride = 0, matrixStride = 0;
    // Source values are tightly packed, column-major. Reflection describes the destination.
    std::uint32_t arrayCount = 0, matrixRows = 0, matrixColumns = 0, scalarSize = 4, vectorSize = 0;
    bool rowMajor = false;
};
struct Descriptor
{
    std::string name;
    std::uint32_t set = 0, binding = 0, count = 1, blockSize = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkShaderStageFlags stages = 0;
    std::vector<UniformField> fields;
};
struct Program
{
    std::vector<std::uint32_t> vertex, fragment, compute;
    std::vector<Descriptor> descriptors;
};
// Inputs are preprocessed GLSL. Failed compilation leaves output unchanged.
bool compileGraphics(const std::string& vertex, const std::string& fragment, Program& output, std::string& error);
bool compileCompute(const std::string& source, Program& output, std::string& error);
bool validateStage(const std::string& source, VkShaderStageFlagBits stage, std::string& error);
// Missing/short values and padding are zero-filled; count is clamped to reflected array capacity.
std::vector<std::uint8_t> packUniforms(const Descriptor&, const UniformValues&);
} // namespace tinyengine::vk
