#include "vulkan_shader.h"

#include <algorithm>
#include <cstring>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <spirv_reflect.h>
#include <tuple>

namespace tinyengine::vk
{
namespace
{
struct Compiler
{
    bool initialized = glslang::InitializeProcess();
    std::mutex mutex;
    ~Compiler()
    {
        if (initialized)
            glslang::FinalizeProcess();
    }
};
Compiler& compiler()
{
    static Compiler value;
    return value;
}

std::string prepare(const std::string& source, EShLanguage stage)
{
    // The material preprocessor has already expanded includes. Upgrade its GL dialect,
    // keeping all following line numbers and the original clip values in varyings.
    static const std::regex version(R"((^|\n)[ \t]*#version[^\n]*)");
    std::string text = std::regex_replace(source, version, "$1", std::regex_constants::format_first_only);
    // The existing GPU particle source uses the reserved identifier `active`.
    std::string prefix =
        "#version 450 core\n#define gl_VertexID gl_VertexIndex\n#define gl_InstanceID gl_InstanceIndex\n"
        "#define active te_vulkan_active\n";
    if (stage == EShLangVertex)
    {
        prefix += "#define main te_vulkan_original_main\n";
        text += "\n#undef main\nvoid main() { te_vulkan_original_main(); gl_Position.z = (gl_Position.z + "
                "gl_Position.w) * 0.5; }\n";
    }
    return prefix + "#line 1\n" + text;
}

bool parse(glslang::TShader& shader, const std::string& text, EShLanguage stage, std::string& error)
{
    const char* input = text.c_str();
    shader.setStrings(&input, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvInputVulkanRulesRelaxed();
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    shader.setGlobalUniformBlockName(stage == EShLangFragment  ? "TinyFragmentUniforms"
                                     : stage == EShLangCompute ? "TinyComputeUniforms"
                                                               : "TinyVertexUniforms");
    shader.setGlobalUniformSet(0);
    shader.setGlobalUniformBinding(stage == EShLangFragment ? 9 : 8);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgEnhanced);
    if (shader.parse(GetDefaultResources(), 450, false, messages))
        return true;
    error = std::string(stage == EShLangVertex     ? "vertex"
                        : stage == EShLangFragment ? "fragment"
                                                   : "compute") +
            " shader: " + shader.getInfoLog() + shader.getInfoDebugLog();
    return false;
}

struct Reflection
{
    SpvReflectShaderModule module{};
    bool initialized = false;
    explicit Reflection(const std::vector<std::uint32_t>& words)
        : initialized(spvReflectCreateShaderModule(words.size() * sizeof(std::uint32_t), words.data(), &module) ==
                      SPV_REFLECT_RESULT_SUCCESS)
    {
    }
    ~Reflection()
    {
        if (initialized)
            spvReflectDestroyShaderModule(&module);
    }
};

std::string descriptorName(const SpvReflectDescriptorBinding& binding)
{
    if (binding.name && *binding.name)
        return binding.name;
    if (binding.type_description && binding.type_description->type_name)
        return binding.type_description->type_name;
    return "descriptor_" + std::to_string(binding.set) + "_" + std::to_string(binding.binding);
}

bool resource(const SpvReflectDescriptorBinding& binding)
{
    return binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
           binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
           binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
           binding.descriptor_type != SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

using MatrixStrides = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;
void fields(const SpvReflectBlockVariable& block, const std::string& prefix, const MatrixStrides& strides,
            std::vector<UniformField>& output)
{
    for (std::uint32_t i = 0; i < block.member_count; ++i)
    {
        const auto& member = block.members[i];
        const std::string name = prefix + (member.name ? member.name : "");
        if (member.member_count && member.array.dims_count == 0)
        {
            fields(member, name + '.', strides, output);
            continue;
        }
        UniformField value;
        value.name = name;
        value.offset = member.absolute_offset;
        value.size = member.size;
        value.arrayStride = member.array.stride;
        value.matrixStride = member.numeric.matrix.stride;
        // SPIRV-Reflect 1.4.357 drops MatrixStride while descending through an
        // OpTypeArray. Read the authoritative member decoration for matrix arrays.
        if (block.type_description)
        {
            const auto* type = block.type_description->struct_type_description;
            const auto id = type ? type->id : block.type_description->id;
            const auto stride = strides.find({id, i});
            if (stride != strides.end())
                value.matrixStride = stride->second;
        }
        value.matrixRows = member.numeric.matrix.row_count;
        value.matrixColumns = member.numeric.matrix.column_count;
        value.scalarSize = member.numeric.scalar.width ? member.numeric.scalar.width / 8 : 4;
        value.vectorSize = std::max(member.numeric.vector.component_count, 1u);
        value.rowMajor = (member.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) != 0;
        value.arrayCount = 1;
        for (std::uint32_t dimension = 0; dimension < member.array.dims_count; ++dimension)
            value.arrayCount *= member.array.dims[dimension];
        output.push_back(std::move(value));
    }
}

bool equalFields(const std::vector<UniformField>& a, const std::vector<UniformField>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tie(a[i].name, a[i].offset, a[i].size, a[i].arrayStride, a[i].matrixStride, a[i].arrayCount,
                     a[i].matrixRows, a[i].matrixColumns,
                     a[i].rowMajor) != std::tie(b[i].name, b[i].offset, b[i].size, b[i].arrayStride, b[i].matrixStride,
                                                b[i].arrayCount, b[i].matrixRows, b[i].matrixColumns, b[i].rowMajor))
            return false;
    return true;
}

void interfaceLocations(SpvReflectInterfaceVariable& value, const std::string& prefix,
                        std::map<std::string, std::uint32_t>& result)
{
    if (value.built_in >= 0)
        return;
    const std::string name = prefix + (value.name ? value.name : "");
    if (value.location != UINT32_MAX)
        result[name] = value.location;
    for (std::uint32_t i = 0; i < value.member_count; ++i)
        interfaceLocations(value.members[i], name + '.', result);
}

void matchInterface(SpvReflectInterfaceVariable& value, const std::string& prefix,
                    const std::map<std::string, std::uint32_t>& locations, std::vector<std::uint32_t>& words)
{
    if (value.built_in >= 0)
        return;
    const std::string name = prefix + (value.name ? value.name : "");
    const auto found = locations.find(name);
    if (found != locations.end() && value.location != UINT32_MAX && value.word_offset.location < words.size())
        words[value.word_offset.location] = found->second;
    for (std::uint32_t i = 0; i < value.member_count; ++i)
        matchInterface(value.members[i], name + '.', locations, words);
}

bool matchGraphicsInterfaces(Program& program, std::string& error)
{
    if (program.vertex.empty() || program.fragment.empty())
        return true;
    Reflection vertex(program.vertex), fragment(program.fragment);
    if (!vertex.initialized || !fragment.initialized)
    {
        error = "SPIR-V interface reflection failed";
        return false;
    }
    std::map<std::string, std::uint32_t> locations;
    for (std::uint32_t i = 0; i < vertex.module.output_variable_count; ++i)
        interfaceLocations(*vertex.module.output_variables[i], "", locations);
    // glslang link has checked interface types. Its default Vulkan mapper assigns
    // locations per stage; apply the producer's explicit name table to consumers.
    for (std::uint32_t i = 0; i < fragment.module.input_variable_count; ++i)
        matchInterface(*fragment.module.input_variables[i], "", locations, program.fragment);
    return true;
}

// GL has separate binding namespaces. Vulkan does not. Remap every opaque resource
// together, by name across all linked stages, reserving the existing UBO/SSBO slots.
bool reflect(Program& program, std::string& error)
{
    struct Stage
    {
        std::vector<std::uint32_t>* words;
        VkShaderStageFlagBits flag;
    };
    const Stage stages[] = {{&program.vertex, VK_SHADER_STAGE_VERTEX_BIT},
                            {&program.fragment, VK_SHADER_STAGE_FRAGMENT_BIT},
                            {&program.compute, VK_SHADER_STAGE_COMPUTE_BIT}};
    using Key = std::pair<std::uint32_t, std::string>;
    std::map<Key, std::uint32_t> remapped;
    std::map<Key, std::pair<SpvReflectDescriptorType, std::uint32_t>> types;
    std::set<std::pair<std::uint32_t, std::uint32_t>> occupied;
    for (const auto& stage : stages)
    {
        if (stage.words->empty())
            continue;
        Reflection reflection(*stage.words);
        if (!reflection.initialized)
        {
            error = "SPIR-V reflection failed";
            return false;
        }
        for (std::uint32_t i = 0; i < reflection.module.descriptor_binding_count; ++i)
        {
            const auto& binding = reflection.module.descriptor_bindings[i];
            if (!resource(binding))
            {
                occupied.emplace(binding.set, binding.binding);
                continue;
            }
            Key key{binding.set, descriptorName(binding)};
            const auto type = std::make_pair(binding.descriptor_type, binding.count);
            auto [found, inserted] = types.emplace(key, type);
            if (!inserted && found->second != type)
            {
                error = "Incompatible resource across shader stages: " + key.second;
                return false;
            }
            remapped.emplace(key, 0);
        }
    }
    for (auto& [key, binding] : remapped)
    {
        binding = 16;
        while (occupied.count({key.first, binding}))
            ++binding;
        occupied.emplace(key.first, binding);
    }
    for (const auto& stage : stages)
    {
        if (stage.words->empty())
            continue;
        Reflection reflection(*stage.words);
        MatrixStrides strides;
        for (std::size_t offset = 5; offset < stage.words->size();)
        {
            const auto instruction = (*stage.words)[offset];
            const auto count = instruction >> 16;
            if (!count || offset + count > stage.words->size())
            {
                error = "Malformed SPIR-V instruction";
                return false;
            }
            if ((instruction & 0xffff) == SpvOpMemberDecorate && count >= 5 &&
                (*stage.words)[offset + 3] == SpvDecorationMatrixStride)
                strides[{(*stage.words)[offset + 1], (*stage.words)[offset + 2]}] = (*stage.words)[offset + 4];
            offset += count;
        }
        for (std::uint32_t i = 0; i < reflection.module.descriptor_binding_count; ++i)
        {
            const auto& source = reflection.module.descriptor_bindings[i];
            Descriptor value;
            value.name = descriptorName(source);
            value.set = source.set;
            value.binding = resource(source) ? remapped.at({source.set, value.name}) : source.binding;
            if (resource(source))
                (*stage.words)[source.word_offset.binding] = value.binding;
            value.count = source.count;
            value.type = static_cast<VkDescriptorType>(source.descriptor_type);
            value.stages = stage.flag;
            value.blockSize = source.block.padded_size;
            if (!value.blockSize)
                value.blockSize = source.block.size;
            fields(source.block, "", strides, value.fields);
            const auto previous = std::find_if(program.descriptors.begin(), program.descriptors.end(),
                                               [&](const Descriptor& item)
                                               { return item.set == value.set && item.binding == value.binding; });
            if (previous != program.descriptors.end())
            {
                if (previous->name != value.name || previous->type != value.type || previous->count != value.count ||
                    previous->blockSize != value.blockSize || !equalFields(previous->fields, value.fields))
                {
                    error = "Incompatible descriptors at set " + std::to_string(value.set) + " binding " +
                            std::to_string(value.binding);
                    return false;
                }
                previous->stages |= stage.flag;
            }
            else
                program.descriptors.push_back(std::move(value));
        }
    }
    std::sort(program.descriptors.begin(), program.descriptors.end(),
              [](const Descriptor& a, const Descriptor& b)
              { return std::tie(a.set, a.binding) < std::tie(b.set, b.binding); });
    return true;
}

bool compile(const std::string* vertex, const std::string* fragment, const std::string* compute, Program& output,
             std::string& error)
{
    auto& state = compiler();
    std::lock_guard<std::mutex> guard(state.mutex);
    error.clear();
    if (!state.initialized)
    {
        error = "glslang initialization failed";
        return false;
    }
    // The program must be destroyed before its shaders (glslang ownership contract).
    std::vector<std::unique_ptr<glslang::TShader>> shaders;
    glslang::TProgram linked;
    Program candidate;
    const std::tuple<const std::string*, EShLanguage, std::vector<std::uint32_t>*> stages[] = {
        {vertex, EShLangVertex, &candidate.vertex},
        {fragment, EShLangFragment, &candidate.fragment},
        {compute, EShLangCompute, &candidate.compute}};
    for (const auto& [source, language, words] : stages)
    {
        if (!source)
            continue;
        auto shader = std::make_unique<glslang::TShader>(language);
        const auto prepared = prepare(*source, language);
        if (!parse(*shader, prepared, language, error))
            return false;
        linked.addShader(shader.get());
        shaders.push_back(std::move(shader));
    }
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgEnhanced);
    if (!linked.link(messages))
    {
        error = std::string("shader link: ") + linked.getInfoLog() + linked.getInfoDebugLog();
        return false;
    }
    if (!linked.mapIO())
    {
        error = std::string("shader interface mapping: ") + linked.getInfoLog() + linked.getInfoDebugLog();
        return false;
    }
    for (const auto& [source, language, words] : stages)
    {
        if (!source)
            continue;
        spv::SpvBuildLogger logger;
        glslang::SpvOptions options;
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*linked.getIntermediate(language), *words, &logger, &options);
        if (words->empty())
        {
            error = "SPIR-V generation failed: " + logger.getAllMessages();
            return false;
        }
    }
    if (!matchGraphicsInterfaces(candidate, error) || !reflect(candidate, error))
        return false;
    output = std::move(candidate);
    return true;
}
} // namespace

bool compileGraphics(const std::string& vertex, const std::string& fragment, Program& output, std::string& error)
{
    return compile(&vertex, &fragment, nullptr, output, error);
}
bool compileCompute(const std::string& source, Program& output, std::string& error)
{
    return compile(nullptr, nullptr, &source, output, error);
}
bool validateStage(const std::string& source, VkShaderStageFlagBits stage, std::string& error)
{
    Program output;
    if (stage == VK_SHADER_STAGE_VERTEX_BIT)
        return compile(&source, nullptr, nullptr, output, error);
    if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
        return compile(nullptr, &source, nullptr, output, error);
    if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
        return compile(nullptr, nullptr, &source, output, error);
    error = "Unsupported standalone shader stage";
    return false;
}

std::vector<std::uint8_t> packUniforms(const Descriptor& descriptor, const UniformValues& values)
{
    std::vector<std::uint8_t> result(descriptor.blockSize, 0);
    for (const auto& field : descriptor.fields)
    {
        auto found = values.find(field.name);
        if (found == values.end())
            found = values.find(field.name + "[0]");
        if (found == values.end())
            continue;
        const auto& value = found->second;
        const auto inputSize = value.elementSize
                                   ? value.elementSize
                                   : static_cast<std::uint32_t>(value.data.size() / std::max(value.count, 1u));
        auto rows = field.matrixRows, columns = field.matrixColumns;
        if (field.matrixStride && (!rows || !columns))
        {
            const auto vectors = (field.arrayStride ? field.arrayStride : field.size) / field.matrixStride;
            const auto components = field.scalarSize ? inputSize / field.scalarSize : 0;
            if (vectors && components % vectors == 0)
            {
                rows = field.rowMajor ? vectors : components / vectors;
                columns = field.rowMajor ? components / vectors : vectors;
            }
        }
        const bool matrix = rows && columns && field.matrixStride;
        const auto tight = matrix             ? rows * columns * field.scalarSize
                           : field.vectorSize ? field.vectorSize * field.scalarSize
                                              : std::min(inputSize, field.size);
        const std::size_t elementSize = value.elementSize ? value.elementSize : tight;
        const auto capacity =
            field.arrayStride ? (field.arrayCount ? field.arrayCount : field.size / field.arrayStride) : 1u;
        const auto count = std::min(value.count, capacity);
        const auto copy = [&](std::size_t destination, std::size_t source, std::size_t size)
        {
            if (destination >= result.size() || source >= value.data.size())
                return;
            size = std::min({size, result.size() - destination, value.data.size() - source});
            std::memcpy(result.data() + destination, value.data.data() + source, size);
        };
        for (std::uint32_t element = 0; element < count; ++element)
        {
            const std::size_t destination = field.offset + static_cast<std::size_t>(element) * field.arrayStride;
            const std::size_t source = static_cast<std::size_t>(element) * elementSize;
            if (matrix)
                for (std::uint32_t column = 0; column < columns; ++column)
                    for (std::uint32_t row = 0; row < rows; ++row)
                    {
                        const auto input = (column * rows + row) * field.scalarSize;
                        if (input + field.scalarSize > elementSize)
                            continue;
                        const auto output = field.rowMajor ? row * field.matrixStride + column * field.scalarSize
                                                           : column * field.matrixStride + row * field.scalarSize;
                        copy(destination + output, source + input, field.scalarSize);
                    }
            else
                copy(destination, source, std::min<std::size_t>(tight, elementSize));
        }
    }
    return result;
}
} // namespace tinyengine::vk
