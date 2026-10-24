#version 430 core
in vec3 vNormal;
in vec3 vTangent;
in vec2 vUv;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
layout(location=0) out vec4 outBaseColorOpacity;
layout(location=1) out vec4 outNormalRoughness;
layout(location=2) out vec4 outEmissiveMetallic;
layout(location=3) out vec4 outMaterialFlags;
layout(location=4) out vec2 outVelocity;
uniform int uMaterialTwoSided;
uniform float uMaterialMaskCutoff;
#include "Surface.glsl"
/*MATERIAL_DECLARATIONS*/
/*MATERIAL_FUNCTIONS*/
void main()
{
    SurfaceData s = EvaluateSurface(vUv);
#if TE_BLEND == 1
    if (s.Opacity < uMaterialMaskCutoff) discard;
#endif
    outBaseColorOpacity = vec4(s.BaseColor, s.Opacity);
    outNormalRoughness = vec4(SurfaceWorldNormal(s,vNormal,vTangent,uMaterialTwoSided != 0),s.Roughness);
    outEmissiveMetallic = vec4(s.Emissive,s.Metallic);
    outMaterialFlags = vec4(s.AO,float(TE_UNLIT),0.0,0.0);
    vec2 currentNdc = vCurrentClip.xy / max(vCurrentClip.w,0.0001);
    vec2 previousNdc = vPreviousClip.xy / max(vPreviousClip.w,0.0001);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
