#version 330 core
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
uniform mat4 uBones[64];
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat4 uView;
uniform mat4 uShadowViewProjections[3];
uniform int uInstanced;
out vec3 vNormal;
out vec3 vTangent;
out vec3 vWorldPosition;
out vec2 vUv;
out vec4 vLightSpace[3];
out float vViewDepth;
void main()
{
    float totalWeight = dot(aBoneWeights, vec4(1.0));
    mat4 skin = mat4(1.0);
    if (totalWeight > 0.0001)
        skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x + uBones[int(aBoneIndices.y)] * aBoneWeights.y +
               uBones[int(aBoneIndices.z)] * aBoneWeights.z + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
    mat4 model = uInstanced != 0 ? mat4(aInstance0, aInstance1, aInstance2, aInstance3) : uModel;
    vec4 skinnedPosition = skin * vec4(aPosition, 1.0);
    vec4 worldPosition = model * skinnedPosition;
    gl_Position = uInstanced != 0 ? uViewProjection * worldPosition : uMvp * skinnedPosition;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vNormal = normalize(normalMatrix * (mat3(skin) * aNormal));
    vTangent = normalize(normalMatrix * (mat3(skin) * aTangent));
    vWorldPosition = worldPosition.xyz;
    for (int cascade = 0; cascade < 3; ++cascade)
        vLightSpace[cascade] = uShadowViewProjections[cascade] * worldPosition;
    vViewDepth = -(uView * worldPosition).z;
    vUv = aUv;
}
