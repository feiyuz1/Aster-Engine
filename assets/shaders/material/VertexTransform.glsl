#ifndef TE_VERTEX_TRANSFORM_GLSL
#define TE_VERTEX_TRANSFORM_GLSL
#ifndef TE_GEOMETRY
#define TE_GEOMETRY -1
#endif
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec4 aBoneIndices;
layout(location = 5) in vec4 aBoneWeights;
layout(location = 8) in vec4 aInstance0;
layout(location = 9) in vec4 aInstance1;
layout(location = 10) in vec4 aInstance2;
layout(location = 11) in vec4 aInstance3;
layout(location = 12) in vec4 aPreviousInstance0;
layout(location = 13) in vec4 aPreviousInstance1;
layout(location = 14) in vec4 aPreviousInstance2;
layout(location = 15) in vec4 aPreviousInstance3;
uniform mat4 uBones[64];
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat4 uPreviousViewProjection;
uniform mat4 uView;
uniform mat4 uPreviousModel;
uniform mat4 uShadowViewProjections[3];
uniform int uInstanced;
uniform mat4 uLightViewProjection;
mat4 te_SkinMatrix()
{
#if TE_GEOMETRY == 0 || TE_GEOMETRY == 2
    return mat4(1.0);
#else
    float totalWeight = dot(aBoneWeights, vec4(1.0));
    if (totalWeight <= 0.0001) return mat4(1.0);
    return uBones[int(aBoneIndices.x)] * aBoneWeights.x + uBones[int(aBoneIndices.y)] * aBoneWeights.y +
           uBones[int(aBoneIndices.z)] * aBoneWeights.z + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
#endif
}
mat4 te_ModelMatrix()
{
#if TE_GEOMETRY == 2
    return mat4(aInstance0, aInstance1, aInstance2, aInstance3);
#else
    return uInstanced != 0 ? mat4(aInstance0, aInstance1, aInstance2, aInstance3) : uModel;
#endif
}
mat4 te_PreviousModelMatrix()
{
#if TE_GEOMETRY == 2
    return mat4(aPreviousInstance0, aPreviousInstance1, aPreviousInstance2, aPreviousInstance3);
#else
    return uInstanced != 0 ? mat4(aPreviousInstance0, aPreviousInstance1, aPreviousInstance2, aPreviousInstance3) : uPreviousModel;
#endif
}
#endif