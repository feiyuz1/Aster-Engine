#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat4 uLightViewProjection;
out vec3 vNormal;
out vec3 vWorldPosition;
out vec2 vUv;
out vec4 vLightSpace;
void main()
{
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(uModel) * aNormal);
    vWorldPosition = vec3(uModel * vec4(aPosition, 1.0));
    vLightSpace = uLightViewProjection * vec4(vWorldPosition, 1.0);
    vUv = aUv;
}
