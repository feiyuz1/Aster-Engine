#version 430 core
layout(location=0) out vec4 outColor;
layout(location=2) out vec4 outAmbient;
uniform sampler2D uGBufferBase;
uniform sampler2D uGBufferNormal;
uniform sampler2D uGBufferEmissive;
uniform sampler2D uGBufferFlags;
uniform sampler2D uGBufferDepth;
uniform mat4 uInverseViewProjection;
uniform mat4 uView;
uniform mat4 uShadowViewProjections[3];
#include "Lighting.glsl"
void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(uGBufferDepth,pixel,0).r;
    if (depth >= 1.0) discard;
    vec4 base = texelFetch(uGBufferBase,pixel,0);
    vec4 normalRoughness = texelFetch(uGBufferNormal,pixel,0);
    vec4 emissiveMetallic = texelFetch(uGBufferEmissive,pixel,0);
    vec4 flags = texelFetch(uGBufferFlags,pixel,0);
    vec4 world = uInverseViewProjection * vec4(gl_FragCoord.xy/uViewportSize*2.0-1.0,depth*2.0-1.0,1.0);
    vec3 position = world.xyz/world.w;
    float viewDepth = -(uView*vec4(position,1.0)).z;
    vec4 lightSpace[3];
    for (int i=0;i<3;++i) lightSpace[i]=uShadowViewProjections[i]*vec4(position,1.0);
    vec3 color,ambient;
    if (flags.g > 0.5)
    {
        color=base.rgb+emissiveMetallic.rgb;
        ambient=vec3(0.0);
    }
    else EvaluateLighting(base.rgb,normalize(normalRoughness.xyz),emissiveMetallic.a,normalRoughness.a,
                          flags.r,emissiveMetallic.rgb,position,viewDepth,lightSpace,color,ambient);
    outColor=vec4(color,base.a);
    outAmbient=vec4(ambient,base.a);
}
