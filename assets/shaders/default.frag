#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vUv;
in vec4 vLightSpace;
uniform vec4 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform sampler2D uTexture;
uniform int uHasTexture;
uniform sampler2D uShadowMap;
out vec4 outColor;

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
    vec3 projected = vLightSpace.xyz / max(vLightSpace.w, 0.0001);
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - dot(normal, light)), 0.0005);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            shadow += projected.z - bias > texture(uShadowMap, projected.xy + vec2(x, y) * texel).r ? 1.0 : 0.0;
    return shadow / 9.0;
}

void main()
{
    vec4 surface = uHasTexture != 0 ? texture(uTexture, vUv) : vec4(1.0);
    vec3 albedo = uBaseColor.rgb * surface.rgb;
    vec3 normal = normalize(vNormal);
    vec3 view = normalize(uCameraPosition - vWorldPosition);
    vec3 light = normalize(-uLightDirection);
    vec3 halfway = normalize(view + light);
    float nl = max(dot(normal, light), 0.0);
    float nv = max(dot(normal, view), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 fresnel = FresnelSchlick(max(dot(halfway, view), 0.0), f0);
    float distribution = DistributionGGX(normal, halfway, uRoughness);
    float geometry = GeometrySchlickGGX(nv, uRoughness) * GeometrySchlickGGX(nl, uRoughness);
    vec3 specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.0001);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - uMetallic) * albedo / 3.14159265;
    float shadow = ShadowFactor(normal, light);
    vec3 direct = (diffuse + specular) * uLightColor * uLightIntensity * nl * (1.0 - shadow);
    vec3 color = albedo * 0.04 + direct + uEmissive;
    outColor = vec4(color, uBaseColor.a * surface.a);
}
