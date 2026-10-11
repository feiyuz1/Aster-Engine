#ifndef TE_SURFACE_GLSL
#define TE_SURFACE_GLSL
struct SurfaceData
{
    vec3 BaseColor;
    float Metallic;
    float Roughness;
    vec3 NormalTS;
    float AO;
    vec3 Emissive;
    float Opacity;
};
vec3 SurfaceWorldNormal(SurfaceData surface, vec3 geometryNormal, vec3 geometryTangent, bool twoSided)
{
    vec3 n = dot(geometryNormal, geometryNormal) > 0.000001 ? normalize(geometryNormal) : vec3(0.0,0.0,1.0);
    vec3 t = geometryTangent - n * dot(n, geometryTangent);
    if (dot(t,t) < 0.000001) t = abs(n.y) < 0.99 ? cross(vec3(0.0,1.0,0.0),n) : cross(vec3(1.0,0.0,0.0),n);
    t = normalize(t);
    vec3 b = normalize(cross(n,t));
    vec3 normalTS = dot(surface.NormalTS,surface.NormalTS) > 0.000001 ? normalize(surface.NormalTS) : vec3(0.0,0.0,1.0);
    vec3 result = normalize(mat3(t,b,n) * normalTS);
    return twoSided && !gl_FrontFacing ? -result : result;
}
#endif
