#version 430 core
in vec2 vUv;
uniform float uMaterialMaskCutoff;
/*MATERIAL_DECLARATIONS*/
/*MATERIAL_FUNCTIONS*/
void main()
{
#if TE_BLEND == 1
    if (EvaluateOpacity(vUv) < uMaterialMaskCutoff) discard;
#elif TE_BLEND == 2
    float alpha = EvaluateOpacity(vUv);
    float threshold = fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233))) * 43758.5453);
    if (alpha < threshold) discard;
#endif
}
