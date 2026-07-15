#version 330 core
in vec3 vNormal;
in vec3 vTangent;
in vec3 vWorldPosition;
in vec2 vUv;
in vec4 vLightSpace[3];
in float vViewDepth;
uniform vec4 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform vec3 uCameraPosition;
uniform int uLightCount;
uniform vec3 uLightDirections[4];
uniform vec3 uLightColors[4];
uniform float uLightIntensities[4];
uniform sampler2D uTexture;
uniform int uHasTexture;
uniform sampler2D uNormalTexture;
uniform int uHasNormalTexture;
uniform sampler2D uMetallicTexture;
uniform int uHasMetallicTexture;
uniform sampler2D uRoughnessTexture;
uniform int uHasRoughnessTexture;
uniform sampler2D uAoTexture;
uniform int uHasAoTexture;
uniform sampler2D uEmissiveTexture;
uniform int uHasEmissiveTexture;
uniform sampler2D uPackedTexture;
uniform int uHasPackedTexture;
uniform int uPackedLayoutRma;
uniform sampler2D uShadowMaps[3];
uniform float uShadowCascadeSplits[3];
uniform sampler2D uEnvironmentIrradiance;
uniform sampler2D uEnvironmentPrefilter;
uniform sampler2D uEnvironmentBrdfLut;
uniform int uHasEnvironment;
out vec4 outColor;

vec2 DirectionToEquirect(vec3 direction)
{
    direction = normalize(direction);
    return vec2(atan(direction.z, direction.x) / 6.2831853 + 0.5, asin(clamp(direction.y, -1.0, 1.0)) / 3.14159265 + 0.5);
}

vec3 SamplePrefilter(vec3 direction, float roughness)
{
    vec2 uv = DirectionToEquirect(direction);
    float level = clamp(roughness, 0.0, 1.0) * 5.0;
    float lower = floor(level), upper = min(5.0, lower + 1.0);
    vec2 lowerUv = vec2(uv.x, (lower + uv.y) / 6.0);
    vec2 upperUv = vec2(uv.x, (upper + uv.y) / 6.0);
    return mix(texture(uEnvironmentPrefilter, lowerUv).rgb, texture(uEnvironmentPrefilter, upperUv).rgb, fract(level));
}

float DistributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nh = max(dot(normal, halfway), 0.0);
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float nv, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return nv / max(nv * (1.0 - k) + k, 0.0001);
}

vec3 FresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float ShadowFactor(vec3 normal, vec3 light)
{
    int cascade = vViewDepth < uShadowCascadeSplits[0] ? 0 : (vViewDepth < uShadowCascadeSplits[1] ? 1 : 2);
    vec3 projected = vLightSpace[cascade].xyz / max(vLightSpace[cascade].w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - dot(normal, light)), 0.0005);
    vec2 texel = 1.0 / vec2(cascade == 0 ? textureSize(uShadowMaps[0], 0) : (cascade == 1 ? textureSize(uShadowMaps[1], 0) : textureSize(uShadowMaps[2], 0)));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            vec2 uv = projected.xy + vec2(x, y) * texel;
            float depth = cascade == 0 ? texture(uShadowMaps[0], uv).r : (cascade == 1 ? texture(uShadowMaps[1], uv).r : texture(uShadowMaps[2], uv).r);
            shadow += projected.z - bias > depth ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

void main()
{
    vec4 surface = uHasTexture != 0 ? texture(uTexture, vUv) : vec4(1.0);
    vec3 albedo = uBaseColor.rgb * surface.rgb;
    vec3 geometricNormal = normalize(vNormal);
    vec3 tangent = normalize(vTangent - geometricNormal * dot(geometricNormal, vTangent));
    vec3 bitangent = normalize(cross(geometricNormal, tangent));
    vec3 normal = uHasNormalTexture != 0 ? normalize(mat3(tangent, bitangent, geometricNormal) * (texture(uNormalTexture, vUv).xyz * 2.0 - 1.0)) : geometricNormal;
    vec3 packedChannels = uHasPackedTexture != 0 ? texture(uPackedTexture, vUv).rgb : vec3(1.0);
    float packedMetallic = packedChannels.b;
    float packedRoughness = uPackedLayoutRma != 0 ? packedChannels.r : packedChannels.g;
    float packedAo = uPackedLayoutRma != 0 ? packedChannels.g : packedChannels.r;
    float metallic = clamp(uMetallic * (uHasMetallicTexture != 0 ? texture(uMetallicTexture, vUv).b : packedMetallic), 0.0, 1.0);
    float roughness = clamp(uRoughness * (uHasRoughnessTexture != 0 ? texture(uRoughnessTexture, vUv).g : packedRoughness), 0.04, 1.0);
    float ao = uHasAoTexture != 0 ? texture(uAoTexture, vUv).r : packedAo;
    vec3 emissive = uEmissive * (uHasEmissiveTexture != 0 ? texture(uEmissiveTexture, vUv).rgb : vec3(1.0));
    vec3 view = normalize(uCameraPosition - vWorldPosition);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 direct = vec3(0.0);
    for (int index = 0; index < uLightCount; ++index)
    {
        vec3 light = normalize(-uLightDirections[index]);
        vec3 halfway = normalize(view + light);
        float nl = max(dot(normal, light), 0.0);
        float nv = max(dot(normal, view), 0.0);
        vec3 fresnel = FresnelSchlick(max(dot(halfway, view), 0.0), f0);
        float distribution = DistributionGGX(normal, halfway, roughness);
        float geometry = GeometrySchlickGGX(nv, roughness) * GeometrySchlickGGX(nl, roughness);
        vec3 specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.0001);
        vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / 3.14159265;
        float visibility = index == 0 ? 1.0 - ShadowFactor(normal, light) : 1.0;
        direct += (diffuse + specular) * uLightColors[index] * uLightIntensities[index] * nl * visibility;
    }
    vec3 skyAmbient = mix(vec3(0.025, 0.035, 0.055), vec3(0.12, 0.18, 0.28), clamp(normal.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 ambient = albedo * skyAmbient;
    if (uHasEnvironment != 0)
    {
        vec3 irradiance = texture(uEnvironmentIrradiance, DirectionToEquirect(normal)).rgb;
        vec3 reflected = reflect(-view, normal);
        vec3 prefiltered = SamplePrefilter(reflected, roughness);
        vec3 fresnel = FresnelSchlick(max(dot(normal, view), 0.0), f0);
        vec2 brdf = texture(uEnvironmentBrdfLut, vec2(max(dot(normal, view), 0.0), roughness)).rg;
        ambient = irradiance * albedo * (1.0 - metallic) + prefiltered * (fresnel * brdf.x + brdf.y);
    }
    vec3 color = ambient * ao + direct + emissive;
    outColor = vec4(color, uBaseColor.a * surface.a);
}
