#version 430 core
#include "VertexTransform.glsl"
out vec2 vUv;
void main()
{
    gl_Position = uLightViewProjection * te_ModelMatrix() * te_SkinMatrix() * vec4(aPosition,1.0);
    vUv = aUv;
}
