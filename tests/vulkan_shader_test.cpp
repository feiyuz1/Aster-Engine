#include "render/render_shaders.h"
#include "render/vulkan_shader.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spirv_reflect.h>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tinyengine::vk;
namespace
{
int failures = 0;
void check(bool ok, const std::string& label)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << '\n';
    failures += !ok;
}
const Descriptor* descriptor(const Program& program, const std::string& name)
{
    for (const auto& value : program.descriptors)
        if (value.name == name)
            return &value;
    return nullptr;
}
const UniformField* field(const Descriptor& descriptor, const std::string& name)
{
    for (const auto& value : descriptor.fields)
        if (value.name == name)
            return &value;
    return nullptr;
}
UniformValue floats(std::initializer_list<float> data, std::uint32_t elementSize = 0, std::uint32_t count = 1)
{
    UniformValue value;
    value.data.resize(data.size() * sizeof(float));
    std::memcpy(value.data.data(), data.begin(), value.data.size());
    value.elementSize = elementSize;
    value.count = count;
    return value;
}
float at(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    float value = -999;
    if (offset + sizeof(value) <= bytes.size())
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
bool valid(const std::vector<std::uint32_t>& words)
{
    if (words.empty())
        return false;
    static std::uint64_t serial = 0;
    const auto file =
        std::filesystem::temp_directory_path() /
        ("tinyengine-shader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(++serial) + ".spv");
    {
        std::ofstream stream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(std::uint32_t));
        if (!stream)
            return false;
    }
#ifdef TINYENGINE_SPIRV_VAL
    const char* validator = TINYENGINE_SPIRV_VAL;
#else
    const char* validator = "spirv-val";
#endif
#ifdef _WIN32
    const auto status =
        _spawnlp(_P_WAIT, validator, validator, "--target-env", "vulkan1.3", file.string().c_str(), nullptr);
#else
    int status = -1;
    const auto child = fork();
    if (child == 0)
    {
        execlp(validator, validator, "--target-env", "vulkan1.3", file.string().c_str(), nullptr);
        _exit(127);
    }
    if (child > 0)
        waitpid(child, &status, 0);
#endif
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    return status == 0;
}
std::unordered_map<std::string, std::uint32_t> locations(const std::vector<std::uint32_t>& words, bool input)
{
    SpvReflectShaderModule module{};
    std::unordered_map<std::string, std::uint32_t> result;
    if (spvReflectCreateShaderModule(words.size() * 4, words.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        return result;
    auto** values = input ? module.input_variables : module.output_variables;
    const auto count = input ? module.input_variable_count : module.output_variable_count;
    for (std::uint32_t i = 0; i < count; ++i)
        if (values[i]->built_in < 0 && values[i]->name)
            result[values[i]->name] = values[i]->location;
    spvReflectDestroyShaderModule(&module);
    return result;
}
void graphicsAndPacking()
{
    const std::string vertex = R"(#version 330 core
layout(location=0) in vec3 position;
out vec2 uv; out vec3 normal;
uniform mat4 uBones[64]; uniform vec3 tint; uniform float weights[3];
uniform sampler2D sharedTexture;
void main() { gl_Position=uBones[0]*vec4(position,1); uv=vec2(weights[1]); normal=tint+texture(sharedTexture,uv).xyz; }
)";
    // Reversed declarations catch independent automatic location assignment.
    const std::string fragment = R"(#version 430 core
in vec3 normal; in vec2 uv;
layout(location=0) out vec4 color;
layout(std140,binding=2) uniform MaterialParameters { vec4 te_parameters[64]; };
uniform sampler2D sharedTexture; uniform sampler2D textures[3]; uniform float exposure;
void main() { color=(texture(sharedTexture,uv)+texture(textures[2],uv))*vec4(normal,1)*te_parameters[0]*exposure; }
)";
    Program p;
    std::string error;
    if (!compileGraphics(vertex, fragment, p, error))
    {
        check(false, "GL plain uniforms compile: " + error);
        return;
    }
    check(valid(p.vertex) && valid(p.fragment), "graphics SPIR-V validates under Vulkan 1.3");
    const auto outputs = locations(p.vertex, false), inputs = locations(p.fragment, true);
    check(outputs.count("uv") && inputs.count("uv") && outputs.at("uv") == inputs.at("uv") &&
              outputs.at("normal") == inputs.at("normal"),
          "reordered varying declarations link by name");
    const Descriptor *vs = nullptr, *fs = nullptr;
    for (const auto& d : p.descriptors)
    {
        if (d.binding == 8 && d.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            vs = &d;
        if (d.binding == 9 && d.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            fs = &d;
    }
    check(vs && fs, "plain uniforms use distinct stage blocks at bindings 8 and 9");
    const auto* texture = descriptor(p, "sharedTexture");
    check(texture && texture->binding >= 16 &&
              texture->stages == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
          "shared sampler has one collision-free binding and both stages");
    const auto* textures = descriptor(p, "textures");
    check(textures && textures->count == 3 && textures->binding >= 16, "sampler arrays preserve descriptor count");
    const auto* material = descriptor(p, "MaterialParameters");
    check(material && material->binding == 2 && material->blockSize == 1024, "material std140 block retains binding 2");
    if (!vs)
        return;
    const auto *bones = field(*vs, "uBones"), *weights = field(*vs, "weights"), *tint = field(*vs, "tint");
    check(bones && bones->arrayStride == 64 && bones->matrixStride == 16 && bones->arrayCount == 64,
          "bones expose matrix and array strides");
    check(weights && weights->arrayStride == 16 && tint && tint->size == 12,
          "scalar arrays and vec3 reflect exact layout");
    UniformValues values;
    values["weights"] = floats({2, 3, 4}, 4, 3);
    values["tint"] = floats({7, 8, 9});
    values["uBones"] = floats({1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                               17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32},
                              64, 2);
    auto bytes = packUniforms(*vs, values);
    check(weights && at(bytes, weights->offset) == 2 && at(bytes, weights->offset + 16) == 3 &&
              at(bytes, weights->offset + 32) == 4 && at(bytes, weights->offset + 4) == 0,
          "scalar arrays scatter into std140 strides with zero padding");
    check(bones && at(bytes, bones->offset + 60) == 16 && at(bytes, bones->offset + 64) == 17 &&
              at(bytes, bones->offset + 128) == 0,
          "two tightly packed bones copy; unspecified bones are zero");
    check(tint && at(bytes, tint->offset + 8) == 9, "vec3 copies twelve bytes");
    auto previous = p.vertex;
    check(!compileGraphics(vertex, "#version 450\ninvalid shader", p, error) && !error.empty() && p.vertex == previous,
          "failed compile reports diagnostics and preserves previous program");
    check(!compileGraphics(vertex, "#version 450\nin vec4 uv; out vec4 c; void main(){c=uv;}", p, error),
          "incompatible stage interfaces are rejected");
}
void computeAndMatrices()
{
    Program p;
    std::string error;
    const auto source = R"(#version 430 core
layout(local_size_x=16) in;
layout(std430,binding=3) buffer Particles { vec4 particles[]; };
layout(std140,row_major,binding=4) uniform Matrices { mat2x3 transform[2]; };
uniform float deltaTime;
void main() { particles[gl_GlobalInvocationID.x]+=vec4(transform[1][0],deltaTime); }
)";
    if (!compileCompute(source, p, error))
    {
        check(false, "compute compiles: " + error);
        return;
    }
    check(valid(p.compute), "compute SPIR-V validates");
    const auto* storage = descriptor(p, "Particles");
    check(storage && storage->binding == 3 && storage->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          "SSBO binding remains unchanged");
    const auto* matrices = descriptor(p, "Matrices");
    if (!matrices)
    {
        check(false, "matrix block reflected");
        return;
    }
    const auto* transform = field(*matrices, "transform");
    check(transform && transform->rowMajor && transform->matrixRows == 3 && transform->matrixColumns == 2,
          "non-square row-major matrix metadata reflected");
    auto bytes = packUniforms(*matrices, {{"transform", floats({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 24, 2)}});
    check(transform && at(bytes, transform->offset) == 1 && at(bytes, transform->offset + 4) == 4 &&
              at(bytes, transform->offset + 16) == 2 && at(bytes, transform->offset + 32) == 3 &&
              at(bytes, transform->offset + 48) == 7 && at(bytes, transform->offset + 52) == 10,
          "column-major CPU matrix arrays scatter into row-major matrix and array strides");
}
void descriptorCollisions()
{
    Program p;
    std::string error;
    const std::string vertex = "#version 450\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    const std::string fragment = R"(#version 450
layout(std430,binding=16) readonly buffer Data { vec4 value; };
layout(std140,binding=2) uniform MaterialParameters { vec4 color; };
uniform sampler2D textureA;
out vec4 outColor;
void main(){outColor=texture(textureA,vec2(.5))*color*value;}
)";
    const bool ok = compileGraphics(vertex, fragment, p, error);
    const auto* image = descriptor(p, "textureA");
    const auto* data = descriptor(p, "Data");
    check(ok && image && image->binding == 17 && data && data->binding == 16 && valid(p.fragment),
          "opaque resource allocation skips occupied storage binding 16: " + error);
    const auto previous = p.fragment;
    const bool collision = compileCompute("#version 450\nlayout(local_size_x=1) in; layout(binding=8) buffer Data { "
                                          "float x; }; uniform float dt; void main(){x+=dt;}",
                                          p, error);
    check(!collision && !error.empty() && p.fragment == previous,
          "uniform/storage binding collision rejects candidate atomically");
}
void explicitLayoutPacking()
{
    Descriptor d;
    d.blockSize = 112;
    // Public callers can supply the original five-field layout without optional type metadata.
    d.fields = {{"normal", 0, 12, 0, 0}, {"weights", 16, 32, 16, 0}, {"basis", 48, 48, 0, 16}};
    auto bytes = packUniforms(d, {{"normal", floats({1, 2, 3})},
                                  {"weights", floats({4, 5}, 4, 2)},
                                  {"basis", floats({6, 7, 8, 9, 10, 11, 12, 13, 14}, 36)}});
    check(at(bytes, 8) == 3 && at(bytes, 16) == 4 && at(bytes, 32) == 5 && at(bytes, 20) == 0,
          "five-field public layouts pack vectors and scalar arrays without optional metadata");
    check(at(bytes, 48) == 6 && at(bytes, 56) == 8 && at(bytes, 60) == 0 && at(bytes, 64) == 9 && at(bytes, 88) == 14,
          "five-field public layouts infer square matrix columns and preserve padding");
}
std::string expanded(const std::filesystem::path& path, int geometry)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("missing material fixture: " + path.string());
    std::ostringstream out;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("#include") != std::string::npos)
        {
            const auto first = line.find('"'), last = line.find('"', first + 1);
            out << expanded(path.parent_path() / line.substr(first + 1, last - first - 1), geometry);
        }
        else if (line.find("/*MATERIAL_DECLARATIONS*/") != std::string::npos)
            out << "layout(std140,binding=2) uniform MaterialParameters { vec4 te_parameters[64]; };\nuniform "
                   "sampler2D te_texture_0;\n";
        else if (line.find("/*MATERIAL_FUNCTIONS*/") != std::string::npos)
        {
            if (path.filename() != "ShadowPass.frag")
                out << "SurfaceData EvaluateSurface(vec2 uv) { SurfaceData s; "
                       "s.BaseColor=texture(te_texture_0,uv).rgb*te_parameters[0].rgb; s.Metallic=0; s.Roughness=.5; "
                       "s.NormalTS=vec3(0,0,1); s.AO=1; s.Emissive=vec3(0); s.Opacity=te_parameters[0].a; return s; "
                       "}\n";
            out << "float EvaluateOpacity(vec2 uv) { return texture(te_texture_0,uv).a*te_parameters[0].a; }\n";
        }
        else
        {
            out << line << '\n';
            if (line.rfind("#version", 0) == 0)
                out << "#define TE_GEOMETRY " << geometry << "\n#define TE_BLEND 1\n#define TE_UNLIT 0\n";
        }
    }
    return out.str();
}
void materialVariants()
{
#ifdef TINYENGINE_SOURCE_DIR
    const std::filesystem::path root = TINYENGINE_SOURCE_DIR;
#else
    const std::filesystem::path root = std::filesystem::current_path();
#endif
    for (const auto* pass : {"BasePass", "ShadowPass", "GBufferPass"})
        for (int geometry = 0; geometry < 3; ++geometry)
        {
            Program p;
            std::string error;
            const auto path = root / "assets/shaders/material";
            const bool ok = compileGraphics(expanded(path / (std::string(pass) + ".vert"), geometry),
                                            expanded(path / (std::string(pass) + ".frag"), geometry), p, error);
            check(ok && valid(p.vertex) && valid(p.fragment),
                  std::string(pass) + " geometry " + std::to_string(geometry) + ": " + error);
        }
    Program p;
    std::string error;
    const bool legacy = compileGraphics(expanded(root / "assets/shaders/default.vert", 0),
                                        expanded(root / "assets/shaders/default.frag", 0), p, error);
    check(legacy && valid(p.vertex) && valid(p.fragment), "legacy default material shader: " + error);
    const bool deferred =
        compileGraphics(tinyengine::RenderShaders::kToneVertexShaderSource,
                        expanded(root / "assets/shaders/material/DeferredLighting.frag", 0), p, error);
    check(deferred && valid(p.vertex) && valid(p.fragment), "shared deferred lighting shader: " + error);
}
void builtinShaders()
{
    using namespace tinyengine::RenderShaders;
    const std::pair<const char*, const char*> pairs[] = {
        {kShadowVertexShaderSource, kShadowFragmentShaderSource},
        {kUiVertexShaderSource, kUiFragmentShaderSource},
        {kParticleVertexShaderSource, kParticleFragmentShaderSource},
        {kGpuParticleVertexShaderSource, kGpuParticleFragmentShaderSource},
        {kToneVertexShaderSource, kSkyFragmentShaderSource},
        {kToneVertexShaderSource, kHzbFragmentShaderSource},
        {kToneVertexShaderSource, kGtaoFragmentShaderSource},
        {kToneVertexShaderSource, kGtaoFilterFragmentShaderSource},
        {kToneVertexShaderSource, kBloomFragmentShaderSource},
        {kToneVertexShaderSource, kTaaFragmentShaderSource},
        {kToneVertexShaderSource, kFinalToneFragmentShaderSource},
        {kToneVertexShaderSource, kPresentFragmentShaderSource},
        {kToneVertexShaderSource, kRetroFragmentShaderSource}};
    int i = 0;
    for (const auto& [vertex, fragment] : pairs)
    {
        Program p;
        std::string error;
        const bool ok = compileGraphics(vertex, fragment, p, error);
        check(ok && valid(p.vertex) && valid(p.fragment),
              "shared builtin graphics " + std::to_string(i++) + ": " + error);
    }
    Program p;
    std::string error;
    const bool culling = compileCompute(kGpuCullComputeShaderSource, p, error);
    check(culling && valid(p.compute), "shared GPU culling compute: " + error);
}
} // namespace
int main()
{
    graphicsAndPacking();
    computeAndMatrices();
    descriptorCollisions();
    explicitLayoutPacking();
    builtinShaders();
    try
    {
        materialVariants();
    }
    catch (const std::exception& e)
    {
        check(false, e.what());
    }
    return failures ? 1 : 0;
}
