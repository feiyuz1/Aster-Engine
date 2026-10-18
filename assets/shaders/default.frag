#version 330 core
in vec3 vNormal;
in vec3 vTangent;
in vec3 vWorldPosition;
in vec2 vUv;
in vec4 vLightSpace[3];
in float vViewDepth;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
uniform vec4 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;







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






layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;
layout(location = 2) out vec4 outAmbient;

#include "material/Lighting.glsl"
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
    vec3 color, ambient;
    EvaluateLighting(albedo, normal, metallic, roughness, ao, emissive, color, ambient);
    if (uViewportRenderMode != 0)
    {
        color = uViewportRenderMode == 2 ? vec3(0.65,0.75,0.9) : albedo;
        ambient = vec3(0.0);
    }
    outColor = vec4(color, uBaseColor.a * surface.a);
    outAmbient = vec4(ambient, outColor.a);
    vec2 currentNdc = vCurrentClip.xy / max(vCurrentClip.w, 0.0001);
    vec2 previousNdc = vPreviousClip.xy / max(vPreviousClip.w, 0.0001);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
