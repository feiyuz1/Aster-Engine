#version 330 core
#include "material/VertexTransform.glsl"
out vec3 vNormal;
out vec3 vTangent;
out vec3 vWorldPosition;
out vec2 vUv;
out vec4 vLightSpace[3];
out float vViewDepth;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
void main()
{
    mat4 skin = te_SkinMatrix();
    mat4 model = te_ModelMatrix();
    mat4 previousModel = te_PreviousModelMatrix();
    vec4 skinnedPosition = skin * vec4(aPosition, 1.0);
    vec4 worldPosition = model * skinnedPosition;
    gl_Position = uInstanced != 0 ? uViewProjection * worldPosition : uMvp * skinnedPosition;
    vCurrentClip = gl_Position;
    vec4 previousWorld = previousModel * skinnedPosition;
    vPreviousClip = uPreviousViewProjection * previousWorld;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vNormal = normalize(normalMatrix * (mat3(skin) * aNormal));
    vTangent = normalize(normalMatrix * (mat3(skin) * aTangent));
    vWorldPosition = worldPosition.xyz;
    for (int cascade = 0; cascade < 3; ++cascade)
        vLightSpace[cascade] = uShadowViewProjections[cascade] * worldPosition;
    vViewDepth = -(uView * worldPosition).z;
    vUv = aUv;
}
