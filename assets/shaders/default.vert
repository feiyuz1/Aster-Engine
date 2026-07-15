#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec4 aBoneIndices;
layout(location = 5) in vec4 aBoneWeights;
uniform mat4 uBones[64];
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uLightViewProjection;
out vec3 vNormal;
out vec3 vTangent;
out vec3 vWorldPosition;
out vec2 vUv;
out vec4 vLightSpace;
void main()
{
    float totalWeight = dot(aBoneWeights, vec4(1.0));
    mat4 skin = mat4(1.0);
    if (totalWeight > 0.0001)
        skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x + uBones[int(aBoneIndices.y)] * aBoneWeights.y +
               uBones[int(aBoneIndices.z)] * aBoneWeights.z + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 skinnedPosition = skin * vec4(aPosition, 1.0);
    gl_Position = uMvp * skinnedPosition;
    vNormal = normalize(mat3(uModel) * aNormal);
    vTangent = normalize(mat3(uModel) * aTangent);
    vWorldPosition = vec3(uModel * vec4(aPosition, 1.0));
    vLightSpace = uLightViewProjection * vec4(vWorldPosition, 1.0);
    vUv = aUv;
}
