#pragma once

#include "tinyengine/math/math.h"
#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tinyengine
{
class TextureResource;
enum class MaterialParameterType
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Texture2D
};
using MaterialParameterValue = std::variant<float, Vec2, Vec3, Vec4, std::string>;
enum class TextureSampling
{
    SRgb,
    Linear,
    Normal
};
enum class MaterialShadingModel
{
    Lit,
    Unlit
};
enum class MaterialBlendMode
{
    Opaque,
    Masked,
    Alpha
};
enum class MaterialDiagnosticSeverity
{
    Warning,
    Error
};

struct MaterialParameterDecl
{
    std::string name;
    MaterialParameterType type = MaterialParameterType::Float;
    MaterialParameterValue defaultValue = 0.0f;
    std::string displayName, group;
    std::optional<Vec2> range;
    TextureSampling sampling = TextureSampling::Linear;
};
struct MaterialRenderState
{
    MaterialShadingModel shadingModel = MaterialShadingModel::Lit;
    MaterialBlendMode blendMode = MaterialBlendMode::Opaque;
    bool twoSided = false;
    float maskCutoff = .5f;
};
struct MaterialDiagnostic
{
    MaterialDiagnosticSeverity severity = MaterialDiagnosticSeverity::Error;
    std::string assetPath;
    std::uint32_t nodeId = 0;
    int port = -1;
    std::string message;
};
struct LegacyMaterialData
{
    Vec4 baseColor{.3f, .7f, 1.f, 1.f};
    float metallic = 0, roughness = .7f;
    Vec3 emissive{};
    bool transparent = false;
    std::string shaderPath, texturePath, normalTexturePath, metallicTexturePath, roughnessTexturePath, aoTexturePath,
        emissiveTexturePath, packedTexturePath, packedTextureLayout = "ORM";
    std::map<std::string, bool> overrideFlags;
    std::map<std::string, Vec4> parameters;
    std::string source;
};
struct MaterialAssetData
{
    int formatVersion = 1;
    std::string definitionPath, parentPath;
    std::map<std::string, MaterialParameterValue> overrides;
    // Provenance of overrides previously validated against a declaration.
    // Keep their values recoverable when the parent schema changes.
    std::map<std::string, MaterialParameterType> validatedOverrideTypes;
    LegacyMaterialData legacyData;
};
struct MaterialParameterSlot
{
    std::string name;
    MaterialParameterType type = MaterialParameterType::Float;
    std::uint32_t slot = 0;
    TextureSampling sampling = TextureSampling::Linear;
};
struct MaterialParameterLayout
{
    std::vector<MaterialParameterSlot> numericSlots, textureSlots;
    std::string canonicalSchema;
};
enum class MaterialPass
{
    Base,
    Shadow
};
enum class MaterialGeometry
{
    Static,
    Skinned,
    Instanced
};
struct CompiledMaterial
{
    std::string parameterSource, surfaceSource, opacitySource;
    MaterialParameterLayout layout;
    MaterialRenderState renderState;
    std::string canonicalProgramContent;
    std::vector<std::string> dependencies;
    std::uint32_t compilerVersion = 1;
};
struct MaterialCompileResult
{
    std::shared_ptr<const CompiledMaterial> material;
    std::vector<MaterialDiagnostic> diagnostics;
    bool Success() const
    {
        return material != nullptr;
    }
};
struct MaterialTemplateSources
{
    std::unordered_map<std::string, std::string> files;
};
struct GeneratedShaderSources
{
    std::string vertex, fragment;
    std::vector<std::string> dependencies;
    std::string error;
};
struct MaterialTextureBinding
{
    std::string parameterName;
    std::uint32_t slot = 0;
    std::shared_ptr<TextureResource> resource;
    TextureSampling sampling = TextureSampling::Linear;
    std::uint64_t version = 0;
    std::string requestedPath;
};
struct MaterialRenderProxy
{
    std::shared_ptr<const CompiledMaterial> compiled;
    std::vector<Vec4> numericSlots;
    std::vector<MaterialTextureBinding> textures;
    MaterialRenderState state;
    std::uint64_t generation = 0, valueHash = 0;
    std::string canonicalValueContent;
    std::string legacyShaderPath;
    std::map<std::string, Vec4> legacyParameters;
};

bool isMaterialParameterName(const std::string& name);
MaterialParameterType getMaterialParameterType(const MaterialParameterValue& value);
const char* materialParameterTypeName(MaterialParameterType type);
bool parseMaterialParameterType(const std::string& text, MaterialParameterType& type);
Vec4 materialNumericSlot(const MaterialParameterValue& value);
bool isFiniteMaterialValue(const MaterialParameterValue& value);
bool readMaterialValue(std::istream& input, MaterialParameterType type, MaterialParameterValue& value);
void writeMaterialValue(std::ostream& output, const MaterialParameterValue& value);
bool parseMaterialAsset(const std::string& source, MaterialAssetData& data, std::string& error);
std::string serializeMaterialAsset(const MaterialAssetData& data);
} // namespace tinyengine
