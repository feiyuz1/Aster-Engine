#version 430 core
in vec3 vNormal;
in vec3 vTangent;
in vec3 vWorldPosition;
in vec2 vUv;
in vec4 vLightSpace[3];
in float vViewDepth;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
layout(location=0) out vec4 outColor;
layout(location=1) out vec2 outVelocity;
layout(location=2) out vec4 outAmbient;
uniform int uMaterialTwoSided;
uniform float uMaterialMaskCutoff;
#include "Surface.glsl"
#include "Lighting.glsl"
/*MATERIAL_DECLARATIONS*/
/*MATERIAL_FUNCTIONS*/
void main()
{
    SurfaceData s = EvaluateSurface(vUv);
#if TE_BLEND == 1
    if (s.Opacity < uMaterialMaskCutoff) discard;
#endif
    vec3 color, ambient;
    if (uViewportRenderMode != 0)
    {
        color = uViewportRenderMode == 2 ? vec3(0.65,0.75,0.9) : s.BaseColor;
        ambient = vec3(0.0);
    }
    else
    {
#if TE_UNLIT == 1
    color = s.BaseColor + s.Emissive;
    ambient = vec3(0.0);
#else
    vec3 normal = SurfaceWorldNormal(s,vNormal,vTangent,uMaterialTwoSided != 0);
    EvaluateLighting(s.BaseColor,normal,s.Metallic,s.Roughness,s.AO,s.Emissive,color,ambient);
#endif
    }
    outColor = vec4(color,s.Opacity);
    outAmbient = vec4(ambient,s.Opacity);
    vec2 currentNdc = vCurrentClip.xy / max(vCurrentClip.w,0.0001);
    vec2 previousNdc = vPreviousClip.xy / max(vPreviousClip.w,0.0001);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
