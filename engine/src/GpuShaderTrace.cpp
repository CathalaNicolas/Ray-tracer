#include "GpuShaders.hpp"

const char *kFragmentShader = R"glsl(
#version 330 core

out vec4 fragColor;

uniform vec3 uOrigin;
uniform vec3 uLowerLeft;
uniform vec3 uHorizontal;
uniform vec3 uVertical;
uniform vec3 uAmbient;
uniform vec3 uHorizon;
uniform vec3 uZenith;
uniform int uWidth;
uniform int uHeight;
uniform int uSamples;
uniform int uSampleOffset;
uniform int uSampleBatch;
uniform int uDepth;
uniform int uHasGlass;
uniform int uSelected;
uniform int uLinear;

uniform int uSphereCount;
uniform int uMirrorCount;
uniform int uMirrorIndex[8];
uniform vec4 uSphereGeom[MAX_SPHERES];
uniform vec4 uSphereAlbedo[MAX_SPHERES];
uniform vec4 uSphereMat[MAX_SPHERES];
uniform int uSphereId[MAX_SPHERES];

uniform int uPlaneCount;
uniform vec4 uPlanePoint[MAX_PLANES];
uniform vec4 uPlaneNormal[MAX_PLANES];
uniform vec4 uPlaneAlbedo[MAX_PLANES];
uniform vec4 uPlaneMat[MAX_PLANES];
uniform vec4 uPlaneChecker[MAX_PLANES];
uniform int uPlaneId[MAX_PLANES];

uniform int uLightCount;
uniform vec4 uLightPos[MAX_LIGHTS];
uniform vec4 uLightColor[MAX_LIGHTS];
uniform vec4 uLightAux[MAX_LIGHTS];
uniform vec4 uLightSpot[MAX_LIGHTS];

uniform vec4 uSphereOpt[MAX_SPHERES];
uniform vec4 uPlaneOpt[MAX_PLANES];
uniform sampler2D uMaterial;

uniform int uMeshCount;
uniform sampler2D uInstances;
uniform sampler2D uMeshPos;
uniform sampler2D uMeshNorm;
uniform sampler2D uMeshUv;
uniform sampler2DArray uShadowMap;
uniform mat4 uShadowMatrix[MAX_LIGHTS];
uniform sampler2D uTris;
uniform sampler2D uBvh;
uniform int uMeshTraceLimit;
uniform int uJobStackLimit;
uniform int uTraceLimit;
uniform int uMeshStackLimit;

uniform sampler2DArray uAlbedo;
uniform sampler2DArray uNormal;
uniform sampler2D uEnv;
uniform int uHasEnv;
uniform float uAperture;
uniform float uFocus;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uView;
uniform float uExposure;
uniform float uTime;
uniform int uParticleCount;
uniform vec4 uParticlePos[16];
uniform vec4 uParticleColor[16];
uniform vec3 uFogColor;
uniform float uFogDensity;

const float EPS = 0.0001;
const float PI = 3.14159265;

struct Hit
{
    bool ok;
    float t;
    vec3 point;
    vec3 normal;
    vec3 albedo;
    float ambient;
    float diffuse;
    float specular;
    float shininess;
    float reflectivity;
    float transmission;
    float ior;
    float uvScale;
    float emission;
    float roughness;
    float front;
    vec2 uv;
    vec2 uvScroll;
    vec3 tangent;
    int texLayer;
    int normalLayer;
    int id;
};

vec3 background(vec3 dir)
{
    vec3 unit = normalize(dir);
    if (uHasEnv == 1)
    {
        float u = atan(unit.z, unit.x) * 0.15915494 + 0.5;
        float v = asin(clamp(unit.y, -1.0, 1.0)) * 0.31830989 + 0.5;
        return texture(uEnv, vec2(u, v)).rgb * uExposure;
    }
    float t = clamp(0.5 * (unit.y + 1.0), 0.0, 1.0);
    return mix(uHorizon, uZenith, t);
}

void blankHit(inout Hit hit)
{
    hit.ok = false;
    hit.transmission = 0.0;
    hit.ior = 1.5;
    hit.uvScale = 1.0;
    hit.emission = 0.0;
    hit.roughness = -1.0;
    hit.front = 1.0;
    hit.uv = vec2(0.0);
    hit.uvScroll = vec2(0.0);
    hit.tangent = vec3(0.0);
    hit.texLayer = -1;
    hit.normalLayer = -1;
    hit.id = -1;
}

void applyOpt(inout Hit hit, vec4 opt, vec2 uv)
{
    hit.transmission = clamp(opt.x, 0.0, 1.0);
    hit.ior = max(opt.y, 1.0);
    hit.uvScale = max(opt.w, 0.0001);
    hit.uv = uv;
    hit.texLayer = opt.z < 0.0 ? -1 : int(opt.z + 0.5);
}

bool hitSphere(vec3 origin, vec3 dir, vec4 geom, float tMin, float tMax, out float t, out vec3 normal, out float front, out vec2 uv)
{
    vec3 center = geom.xyz;
    float radius = geom.w;
    if (radius <= 0.0)
        return false;
    vec3 oc = origin - center;
    float a = dot(dir, dir);
    float halfB = dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float disc = halfB * halfB - a * c;
    if (disc < 0.0 || a == 0.0)
        return false;
    float root = sqrt(disc);
    float nearT = (-halfB - root) / a;
    if (nearT < tMin || nearT > tMax)
    {
        nearT = (-halfB + root) / a;
        if (nearT < tMin || nearT > tMax)
            return false;
    }
    t = nearT;
    vec3 outward = normalize((origin + dir * t) - center);
    front = dot(dir, outward) < 0.0 ? 1.0 : 0.0;
    normal = front > 0.5 ? outward : -outward;
    uv = vec2(
        atan(outward.z, outward.x) * 0.15915494 + 0.5,
        asin(clamp(outward.y, -1.0, 1.0)) * 0.31830989 + 0.5);
    return true;
}

bool hitPlane(vec3 origin, vec3 dir, vec4 point, vec4 normalIn, float tMin, float tMax, out float t, out vec3 normal, out float front)
{
    vec3 n = normalIn.xyz;
    float denom = dot(dir, n);
    if (abs(denom) < 1e-8)
        return false;
    t = dot(point.xyz - origin, n) / denom;
    if (t < tMin || t > tMax)
        return false;
    front = denom < 0.0 ? 1.0 : 0.0;
    normal = front > 0.5 ? n : -n;
    return true;
}

void consider(inout Hit best, float t, vec3 point, vec3 normal, float front, vec2 uv, vec3 albedo, float ambient, float diffuse, float specular, float shininess, float reflectivity, vec4 opt, vec4 xtra, vec2 scroll, int id)
{
    best.ok = true;
    best.t = t;
    best.point = point;
    best.normal = normal;
    best.front = front;
    best.albedo = albedo;
    best.ambient = ambient;
    best.diffuse = diffuse;
    best.specular = specular;
    best.shininess = shininess;
    best.reflectivity = reflectivity;
    best.emission = xtra.x;
    best.roughness = xtra.z;
    best.normalLayer = int(floor(xtra.y + 0.5));
    best.tangent = vec3(0.0);
    best.uvScroll = scroll;
    best.id = id;
    applyOpt(best, opt, uv);
}

Hit closest(vec3 origin, vec3 dir, float tMin, float tMax)
{
    Hit best;
    best.t = tMax;
    blankHit(best);
    for (int i = 0; i < uSphereCount; ++i)
    {
        float t;
        vec3 n;
        float front;
        vec2 uv;
        if (hitSphere(origin, dir, uSphereGeom[i], tMin, best.t, t, n, front, uv))
            consider(best, t, origin + dir * t, n, front, uv, uSphereAlbedo[i].rgb, uSphereAlbedo[i].a, uSphereMat[i].x, uSphereMat[i].y, uSphereMat[i].z, uSphereMat[i].w, uSphereOpt[i], texelFetch(uMaterial, ivec2(i, 0), 0), texelFetch(uMaterial, ivec2(i, 3), 0).xy, uSphereId[i]);
    }
    for (int i = 0; i < uPlaneCount; ++i)
    {
        float t;
        vec3 n;
        float front;
        if (hitPlane(origin, dir, uPlanePoint[i], uPlaneNormal[i], tMin, best.t, t, n, front))
        {
            vec3 point = origin + dir * t;
            vec3 albedo = uPlaneAlbedo[i].rgb;
            if (uPlaneNormal[i].w > 0.5)
            {
                float scale = max(uPlaneChecker[i].w, 0.0001);
                int ix = int(floor(point.x / scale));
                int iz = int(floor(point.z / scale));
                if (((ix + iz) & 1) != 0)
                    albedo = uPlaneChecker[i].rgb;
            }
            consider(best, t, point, n, front, point.xz, albedo, uPlaneAlbedo[i].a, uPlaneMat[i].x, uPlaneMat[i].y, uPlaneMat[i].z, uPlaneMat[i].w, uPlaneOpt[i], texelFetch(uMaterial, ivec2(i, 1), 0), texelFetch(uMaterial, ivec2(i, 4), 0).xy, uPlaneId[i]);
        }
    }
    if (best.ok && best.texLayer >= 0)
        best.albedo = texture(uAlbedo, vec3(best.uv * best.uvScale + best.uvScroll * uTime, float(best.texLayer))).rgb;
    return best;
}

bool sphereBlocks(vec3 origin, vec3 dir, vec4 geom, float tMin, float tMax)
{
    float radius = geom.w;
    if (radius <= 0.0)
        return false;
    vec3 oc = origin - geom.xyz;
    float a = dot(dir, dir);
    float halfB = dot(oc, dir);
    float disc = halfB * halfB - a * (dot(oc, oc) - radius * radius);
    if (disc < 0.0 || a == 0.0)
        return false;
    float root = sqrt(disc);
    float t = (-halfB - root) / a;
    if (t >= tMin && t <= tMax)
        return true;
    t = (-halfB + root) / a;
    return t >= tMin && t <= tMax;
}

bool planeBlocks(vec3 origin, vec3 dir, vec4 point, vec4 normalIn, float tMin, float tMax)
{
    vec3 n = normalIn.xyz;
    float denom = dot(dir, n);
    if (abs(denom) < 1e-8)
        return false;
    float t = dot(point.xyz - origin, n) / denom;
    return t >= tMin && t <= tMax;
}

bool meshShadow(vec3 point, vec3 normal, int light, vec3 samplePos, bool infinite, int diskIndex, int diskCount, float radius)
{
    if (uMeshCount <= 0 || light < 0)
        return false;
    vec2 uv;
    vec2 cellOrigin = vec2(0.0);
    vec2 cellSize = vec2(1.0);
    bool inMap = true;
    if (!infinite)
    {
        vec3 delta = point - uLightPos[light].xyz;
        float dist = length(delta);
        if (dist <= 1e-5)
            return false;
        vec3 dir = delta / dist;
        int face = 0;
        float major = dir.x;
        vec2 st = vec2(-dir.z, -dir.y);
        if (-dir.x > major) { face = 1; major = -dir.x; st = vec2(dir.z, -dir.y); }
        if (dir.y > major) { face = 2; major = dir.y; st = vec2(dir.x, dir.z); }
        if (-dir.y > major) { face = 3; major = -dir.y; st = vec2(dir.x, -dir.z); }
        if (dir.z > major) { face = 4; major = dir.z; st = vec2(dir.x, -dir.y); }
        if (-dir.z > major) { face = 5; major = -dir.z; st = vec2(-dir.x, -dir.y); }
        vec2 local = clamp((st / major) * 0.5 + 0.5, 0.0, 1.0);
        cellOrigin = vec2(float(face % 3) * 341.0, float(face / 3) * 512.0) / 1024.0;
        cellSize = vec2(341.0, 512.0) / 1024.0;
        uv = cellOrigin + local * cellSize;
    }
    else
    {
        vec4 clip = uShadowMatrix[light] * vec4(point, 1.0);
        if (clip.w <= 0.0001)
            return false;
        vec3 ndc = clip.xyz / clip.w;
        uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || ndc.z < -1.0 || ndc.z > 1.0)
            inMap = false;
    }
    if (!inMap)
        return false;
    if (radius > 0.0001 && diskCount > 1)
    {
        float spread = min(radius * 0.02, 0.04);
        float r = spread * sqrt((float(diskIndex) + 0.5) / float(diskCount));
        float theta = float(diskIndex) * 2.39996323;
        uv += vec2(cos(theta), sin(theta)) * r;
    }
    float stored = 0.0;
    vec2 texel = vec2(1.0 / 1024.0);
    for (int tap = 0; tap < 4; ++tap)
    {
        vec2 offset = texel * vec2(float(tap % 2) - 0.5, float(tap / 2) - 0.5);
        vec2 tapUv = clamp(uv + offset, cellOrigin + texel, cellOrigin + cellSize - texel);
        stored += texture(uShadowMap, vec3(tapUv, float(light))).r;
    }
    stored *= 0.25;
    vec3 toLight = infinite ? normalize(samplePos - point) : samplePos - point;
    float toLen = length(toLight);
    float facing = toLen > 0.0 ? clamp(dot(normal, toLight / toLen), 0.0, 1.0) : 1.0;
    float bias = max(0.004, 0.012 * (1.0 - facing));
    if (infinite)
        return dot(point, -normalize(uLightPos[light].xyz)) > stored + bias;
    return distance(point, uLightPos[light].xyz) > stored + bias;
}

bool occluded(vec3 point, vec3 normal, vec3 lightPos, bool infinite, int light, int diskIndex, int diskCount, float radius, int skipId, int skipId2)
{
    vec3 toLight = lightPos - point;
    float dist = length(toLight);
    if (!infinite && dist <= EPS)
        return true;
    vec3 dir = infinite ? normalize(toLight) : toLight / dist;
    float tMax = infinite ? 1e20 : dist - EPS;
    vec3 origin = point + normal * EPS;
    for (int i = 0; i < uSphereCount; ++i)
    {
        if (uSphereOpt[i].x >= 0.5)
            continue;
        if ((skipId > 0 && uSphereId[i] == skipId) || (skipId2 > 0 && uSphereId[i] == skipId2))
            continue;
        if (sphereBlocks(origin, dir, uSphereGeom[i], EPS, tMax))
            return true;
    }
    for (int i = 0; i < uPlaneCount; ++i)
    {
        if (uPlaneOpt[i].x >= 0.5)
            continue;
        if (planeBlocks(origin, dir, uPlanePoint[i], uPlaneNormal[i], EPS, tMax))
            return true;
    }
    if (meshShadow(point, normal, light, lightPos, infinite, diskIndex, diskCount, radius))
        return true;
    return false;
}

vec3 areaOffset(vec3 direction, float radius, int index, int count)
{
    if (radius <= 0.0001 || count <= 1)
        return vec3(0.0);
    vec3 axis = normalize(direction);
    vec3 helper = abs(axis.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, axis));
    vec3 bitangent = cross(axis, tangent);
    float r = radius * sqrt((float(index) + 0.5) / float(count));
    float theta = float(index) * 2.39996323;
    return (tangent * cos(theta) + bitangent * sin(theta)) * r;
}
)glsl"
R"glsl(
vec3 lightSurface(vec3 dir, Hit hit, float diffuseScale, int lightSample, int cameraSamples)
{
    vec3 color = hit.albedo * hit.ambient * uAmbient * diffuseScale;
    vec3 viewDir = -dir;
    for (int i = 0; i < uLightCount; ++i)
    {
        int emitter = int(floor(uLightAux[i].w + 0.5));
        if (emitter > 0 && emitter == hit.id)
            continue;
        bool directional = uLightAux[i].y > 0.5;
        float radius = max(uLightAux[i].x, 0.0);
        int sampleCount = 1;
        int diskCount = 1;
        if (radius > 0.0001)
        {
            if (cameraSamples > 1)
            {
                diskCount = cameraSamples * cameraSamples;
            }
            else
            {
                sampleCount = 4;
                diskCount = 4;
            }
        }
        vec3 lit = vec3(0.0);
        for (int s = 0; s < sampleCount; ++s)
        {
            int diskIndex = cameraSamples > 1 ? lightSample : s;
            vec3 lightDir;
            float dist;
            float attenuation;
            vec3 samplePos;
            bool infinite = directional;
            if (directional)
            {
                vec3 base = normalize(uLightPos[i].xyz);
                lightDir = normalize(base + areaOffset(base, tan(radius), diskIndex, diskCount));
                dist = 1e20;
                attenuation = uLightPos[i].w;
                samplePos = hit.point + lightDir;
            }
            else
            {
                vec3 center = uLightPos[i].xyz;
                vec3 toCenter = center - hit.point;
                samplePos = center + areaOffset(toCenter, radius, diskIndex, diskCount);
                vec3 toLight = samplePos - hit.point;
                dist = length(toLight);
                if (dist <= EPS)
                    continue;
                lightDir = toLight / dist;
                attenuation = uLightPos[i].w / (1.0 + uLightColor[i].w * dist * dist);
            }
            if (uLightSpot[i].w > 0.0)
            {
                vec3 axis = uLightSpot[i].xyz;
                float axisLen = length(axis);
                if (axisLen <= 1e-6)
                    continue;
                axis /= axisLen;
                float cosOuter = cos(uLightSpot[i].w);
                float innerRad = uLightAux[i].z;
                if (innerRad < 0.0)
                    innerRad = 0.0;
                if (innerRad > uLightSpot[i].w)
                    innerRad = uLightSpot[i].w;
                float cosInner = cos(innerRad);
                float denom = max(cosInner - cosOuter, 1e-4);
                float spot = clamp((dot(axis, -lightDir) - cosOuter) / denom, 0.0, 1.0);
                spot *= spot;
                if (spot <= 0.0)
                    continue;
                attenuation *= spot;
            }
            float nDotL = dot(hit.normal, lightDir);
            if (nDotL <= 0.0)
                continue;
            if (occluded(hit.point, hit.normal, samplePos, infinite, i, diskIndex, diskCount, radius, emitter, 0))
                continue;
            vec3 lightColor = uLightColor[i].rgb;
            lit += hit.albedo * hit.diffuse * lightColor * attenuation * nDotL * diffuseScale;
            float spec = hit.specular;
            if (hit.roughness >= 0.0)
            {
                float remain = 1.0 - clamp(hit.roughness, 0.0, 1.0);
                spec *= remain * remain;
            }
            if (spec > 0.0)
            {
                vec3 halfway = lightDir + viewDir;
                float halfwayLength = length(halfway);
                if (halfwayLength > 0.0)
                {
                    float nDotH = max(dot(hit.normal, halfway / halfwayLength), 0.0);
                    lit += lightColor * (spec * attenuation * pow(nDotH, hit.shininess));
                }
            }
        }
        color += lit / float(sampleCount);
    }
    color += hit.albedo * max(hit.emission, 0.0);
    return color;
}

float schlick(float cosTheta, float n1, float n2)
{
    float r0 = (n1 - n2) / max(n1 + n2, 0.0001);
    r0 *= r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);
}

vec3 refractRay(vec3 dir, vec3 normal, float eta, out bool tir)
{
    float cosI = clamp(dot(-dir, normal), 0.0, 1.0);
    float sin2T = eta * eta * (1.0 - cosI * cosI);
    if (sin2T > 1.0)
    {
        tir = true;
        return vec3(0.0);
    }
    tir = false;
    float cosT = sqrt(max(0.0, 1.0 - sin2T));
    return normalize(eta * dir + (eta * cosI - cosT) * normal);
}

vec4 instanceRow(int mesh, int row)
{
    return texelFetch(uInstances, ivec2(mesh, row), 0);
}

bool commitMesh(inout Hit best, vec3 origin, vec3 dir, vec3 point, vec3 normal, vec2 uv, int mesh, float t, vec3 tangent)
{
    if (mesh < 0 || mesh >= uMeshCount || t <= EPS || (best.ok && t >= best.t))
        return false;
    float nLen = length(normal);
    vec3 n = nLen > 1e-5 ? normal / nLen : vec3(0.0, 1.0, 0.0);
    if (dot(dir, n) > 0.0)
        n = -n;
    vec4 albedo = instanceRow(mesh, 0);
    vec4 material = instanceRow(mesh, 1);
    vec4 options = instanceRow(mesh, 2);
    consider(best, t, point, n, 1.0, uv, albedo.rgb, albedo.a, material.x, material.y, material.z, material.w, options, texelFetch(uMaterial, ivec2(mesh, 2), 0), texelFetch(uMaterial, ivec2(mesh, 5), 0).xy, int(instanceRow(mesh, 4).w + 0.5));
    best.tangent = tangent;
    if (best.texLayer >= 0)
        best.albedo = texture(uAlbedo, vec3(best.uv * best.uvScale + best.uvScroll * uTime, float(best.texLayer))).rgb;
    return true;
}

void applyRasterMesh(inout Hit best, vec3 origin, vec3 dir)
{
    if (uMeshCount <= 0)
        return;
    vec4 gp = texelFetch(uMeshPos, ivec2(gl_FragCoord.xy), 0);
    if (gp.w < 0.0)
        return;
    float t = dot(gp.xyz - origin, dir);
    vec4 gn = texelFetch(uMeshNorm, ivec2(gl_FragCoord.xy), 0);
    vec4 guv = texelFetch(uMeshUv, ivec2(gl_FragCoord.xy), 0);
    commitMesh(best, origin, dir, gp.xyz, gn.xyz, guv.xy, int(gp.w + 0.5), t, vec3(guv.z, guv.w, gn.w));
}

vec4 fetchTexel(sampler2D image, int index)
{
    return texelFetch(image, ivec2(index & 1023, index >> 10), 0);
}

vec3 rayInv(vec3 dir)
{
    return vec3(
        abs(dir.x) > 1e-8 ? 1.0 / dir.x : (dir.x >= 0.0 ? 1e20 : -1e20),
        abs(dir.y) > 1e-8 ? 1.0 / dir.y : (dir.y >= 0.0 ? 1e20 : -1e20),
        abs(dir.z) > 1e-8 ? 1.0 / dir.z : (dir.z >= 0.0 ? 1e20 : -1e20));
}

bool slab(vec3 origin, vec3 invDir, vec3 bmin, vec3 bmax, float tMin, float tMax)
{
    vec3 t0 = (bmin - origin) * invDir;
    vec3 t1 = (bmax - origin) * invDir;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    float tEnter = max(tMin, max(lo.x, max(lo.y, lo.z)));
    float tExit = min(tMax, min(hi.x, min(hi.y, hi.z)));
    return tEnter <= tExit;
}

void orderChildren(float packedLeft, float rightIndex, vec3 bmin, vec3 bmax, vec3 origin, out int nearer, out int farther)
{
    int axis = int(packedLeft * (1.0 / 1048576.0));
    int left = int(packedLeft - float(axis) * 1048576.0 + 0.5);
    int right = int(rightIndex + 0.5);
    float along = axis == 0 ? origin.x : (axis == 1 ? origin.y : origin.z);
    float mid = axis == 0 ? (bmin.x + bmax.x) * 0.5 : (axis == 1 ? (bmin.y + bmax.y) * 0.5 : (bmin.z + bmax.z) * 0.5);
    if (along < mid)
    {
        nearer = left;
        farther = right;
    }
    else
    {
        nearer = right;
        farther = left;
    }
}

// Shared Möller-Trumbore. triBlocks is the any-hit path; hitStoredTri adds shading attrs.
bool hitTriCore(int tri, vec3 origin, vec3 dir, float tMin, float tMax, out float t, out float bu, out float bv, out vec3 edge1, out vec3 edge2, out vec4 a, out vec4 b, out vec4 c)
{
    int base = tri * 6;
    a = fetchTexel(uTris, base);
    b = fetchTexel(uTris, base + 1);
    c = fetchTexel(uTris, base + 2);
    vec3 v0 = a.xyz;
    edge1 = b.xyz - v0;
    edge2 = c.xyz - v0;
    vec3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (abs(det) < 1e-8)
        return false;
    float invDet = 1.0 / det;
    vec3 s = origin - v0;
    bu = dot(s, pvec) * invDet;
    if (bu < 0.0 || bu > 1.0)
        return false;
    vec3 qvec = cross(s, edge1);
    bv = dot(dir, qvec) * invDet;
    if (bv < 0.0 || bu + bv > 1.0)
        return false;
    t = dot(edge2, qvec) * invDet;
    return t >= tMin && t <= tMax;
}

bool triBlocks(int tri, vec3 origin, vec3 dir, float tMin, float tMax)
{
    float t;
    float bu;
    float bv;
    vec3 edge1;
    vec3 edge2;
    vec4 a;
    vec4 b;
    vec4 c;
    return hitTriCore(tri, origin, dir, tMin, tMax, t, bu, bv, edge1, edge2, a, b, c);
}

bool hitStoredTri(int tri, vec3 origin, vec3 dir, float tMin, float tMax, out float t, out vec3 normal, out float front, out vec2 uv, out vec3 tangent)
{
    tangent = vec3(0.0);
    float bu;
    float bv;
    vec3 edge1;
    vec3 edge2;
    vec4 a;
    vec4 b;
    vec4 c;
    if (!hitTriCore(tri, origin, dir, tMin, tMax, t, bu, bv, edge1, edge2, a, b, c))
        return false;
    int base = tri * 6;
    vec4 d = fetchTexel(uTris, base + 3);
    vec4 e = fetchTexel(uTris, base + 4);
    vec4 f = fetchTexel(uTris, base + 5);
    float bw = 1.0 - bu - bv;
    vec3 geo = cross(edge1, edge2);
    float geoLen = length(geo);
    geo = geoLen > 1e-8 ? geo / geoLen : vec3(0.0, 1.0, 0.0);
    front = dot(dir, geo) < 0.0 ? 1.0 : 0.0;
    if (front < 0.5)
        geo = -geo;
    normal = normalize(d.xyz * bw + e.xyz * bu + f.xyz * bv);
    if (dot(normal, geo) < 0.0)
        normal = -normal;
    vec2 uv0 = vec2(a.w, b.w);
    vec2 uv1 = vec2(c.w, d.w);
    vec2 uv2 = vec2(e.w, f.w);
    uv = uv0 * bw + uv1 * bu + uv2 * bv;
    vec2 duv1 = uv1 - uv0;
    vec2 duv2 = uv2 - uv0;
    float uvDet = duv1.x * duv2.y - duv2.x * duv1.y;
    if (abs(uvDet) > 1e-8)
        tangent = (edge1 * duv2.y - edge2 * duv1.y) / uvDet;
    return true;
}

void traceMesh(inout Hit best, vec3 origin, vec3 dir, float tCap, bool anyHit)
{
    if (uMeshCount <= 0)
        return;
    vec3 invDir = rayInv(dir);
    int meshStack[24];
    float limit = best.ok ? min(best.t, tCap) : tCap;
    int stackCap = uMeshStackLimit;
    if (stackCap < 2)
        stackCap = 2;
    if (stackCap > 24)
        stackCap = 24;
    for (int m = 0; m < uMeshCount; ++m)
    {
        vec4 boxMin = instanceRow(m, 3);
        vec4 boxMax = instanceRow(m, 4);
        if (!slab(origin, invDir, boxMin.xyz, boxMax.xyz, EPS, limit))
            continue;
        vec4 place = instanceRow(m, 5);
        vec3 axisX = instanceRow(m, 6).xyz;
        vec3 axisY = instanceRow(m, 7).xyz;
        vec3 axisZ = instanceRow(m, 8).xyz;
        mat3 rotation = mat3(axisX, axisY, axisZ);
        float scale = max(place.w, 1e-8);
        mat3 inverse = transpose(rotation);
        vec3 localOrigin = inverse * (origin - place.xyz) / scale;
        vec3 localDir = inverse * dir / scale;
        vec3 localInv = rayInv(localDir);
        int top = 0;
        meshStack[top++] = int(boxMin.w + 0.5);
        int stepsLeft = uMeshTraceLimit;
        while (stepsLeft > 0)
        {
            stepsLeft -= 1;
            if (top <= 0)
                break;
            int node = meshStack[--top];
            vec4 a = fetchTexel(uBvh, node * 2);
            vec4 b = fetchTexel(uBvh, node * 2 + 1);
            if (!slab(localOrigin, localInv, a.xyz, b.xyz, EPS, limit))
                continue;
            if (b.w < 0.0)
            {
                int count = int(-b.w + 0.5);
                int first = int(a.w + 0.5);
                for (int tri = 0; tri < count; ++tri)
                {
                    if (anyHit)
                    {
                        if (triBlocks(first + tri, localOrigin, localDir, EPS, limit))
                        {
                            best.ok = true;
                            return;
                        }
                        continue;
                    }
                    float t;
                    vec3 n;
                    float front;
                    vec2 uv;
                    vec3 tangent;
                    if (hitStoredTri(first + tri, localOrigin, localDir, EPS, limit, t, n, front, uv, tangent) &&
                        commitMesh(best, origin, dir, origin + dir * t, rotation * n, uv, m, t, rotation * tangent))
                        limit = best.t;
                }
            }
            else if (top < stackCap - 2)
            {
                int nearer;
                int farther;
                orderChildren(a.w, b.w, a.xyz, b.xyz, localOrigin, nearer, farther);
                meshStack[top++] = farther;
                meshStack[top++] = nearer;
            }
        }
    }
}

void shadeNormal(inout Hit hit)
{
    if (hit.normalLayer < 0)
        return;
    vec3 n = hit.normal;
    vec3 t = hit.tangent;
    if (dot(t, t) < 1e-6)
    {
        vec3 helper = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        t = normalize(cross(helper, n));
    }
    else
    {
        t = t - n * dot(n, t);
        if (dot(t, t) < 1e-6)
            return;
        t = normalize(t);
    }
    vec3 b = cross(n, t);
    vec3 detail = texture(uNormal, vec3(hit.uv * hit.uvScale + hit.uvScroll * uTime, float(hit.normalLayer))).xyz * 2.0 - 1.0;
    vec3 mapped = t * detail.x + b * detail.y + n * detail.z;
    if (dot(mapped, mapped) > 1e-8)
        hit.normal = normalize(mapped);
}
)glsl"
R"glsl(
bool segmentBlocked(vec3 from, vec3 to, int skipId, int skipMirror)
{
    vec3 delta = to - from;
    float dist = length(delta);
    if (dist <= EPS)
        return false;
    vec3 dir = delta / dist;
    vec3 origin = from + dir * EPS;
    float tMax = dist - EPS;
    for (int i = 0; i < uSphereCount; ++i)
    {
        if (uSphereOpt[i].x >= 0.5)
            continue;
        int id = uSphereId[i];
        if ((skipId > 0 && id == skipId) || (skipMirror > 0 && id == skipMirror))
            continue;
        if (sphereBlocks(origin, dir, uSphereGeom[i], EPS, tMax))
            return true;
    }
    for (int i = 0; i < uPlaneCount; ++i)
    {
        if (uPlaneOpt[i].x >= 0.5)
            continue;
        if (planeBlocks(origin, dir, uPlanePoint[i], uPlaneNormal[i], EPS, tMax))
            return true;
    }
    Hit mesh;
    blankHit(mesh);
    traceMesh(mesh, origin, dir, tMax, true);
    return mesh.ok;
}

)glsl"
R"glsl(
bool bouncePoint(vec3 center, float radius, vec3 aimPos, bool aimInfinite, vec3 surface, out vec3 Q, out vec3 n, out vec3 toPoint, out float pointDist, out float align, out float cosIn)
{
    vec3 fromCenter = surface - center;
    if (dot(fromCenter, fromCenter) <= radius * radius || radius <= 0.0001)
        return false;
    vec3 lightDirIn = aimInfinite ? normalize(aimPos) : normalize(aimPos - center);
    n = normalize(normalize(fromCenter) + lightDirIn);
    for (int k = 0; k < 4; ++k)
    {
        vec3 q = n * radius;
        vec3 towardSurface = fromCenter - q;
        vec3 towardAim = aimInfinite ? lightDirIn : (aimPos - center) - q;
        float surfaceLen = length(towardSurface);
        float aimLen = aimInfinite ? 1.0 : length(towardAim);
        if (surfaceLen <= EPS || aimLen <= EPS)
            return false;
        towardSurface /= surfaceLen;
        towardAim /= aimLen;
        vec3 bisector = towardSurface + towardAim;
        if (dot(bisector, bisector) <= 1e-6)
            return false;
        vec3 next = normalize(bisector);
        bool settled = dot(next, n) > 0.999;
        n = next;
        if (settled)
            break;
    }
    Q = center + n * radius;
    toPoint = surface - Q;
    pointDist = length(toPoint);
    if (pointDist <= EPS)
        return false;
    toPoint /= pointDist;
    vec3 incident = aimInfinite ? -lightDirIn : normalize(Q - aimPos);
    align = dot(reflect(incident, n), toPoint);
    cosIn = max(dot(n, -incident), 0.0);
    return dot(n, toPoint) > 0.0 && dot(n, -incident) > 0.0;
}

vec3 mirrorBounce(Hit hit)
{
    vec3 extra = vec3(0.0);
    for (int m = 0; m < 8; ++m)
    {
        if (m >= uMirrorCount)
            break;
        int s = uMirrorIndex[m];
        if (s < 0 || s >= uSphereCount)
            continue;
        int mirrorId = uSphereId[s];
        if (mirrorId == hit.id)
            continue;
        vec3 center = uSphereGeom[s].xyz;
        float radius = uSphereGeom[s].w;
        if (radius <= 0.0001)
            continue;
        float reflectW = uSphereMat[s].w;
        vec3 tint = uSphereAlbedo[s].rgb;
        vec3 fromCenter = hit.point - center;
        if (dot(fromCenter, fromCenter) <= radius * radius)
            continue;
        for (int i = 0; i < uLightCount; ++i)
        {
            int emitter = int(floor(uLightAux[i].w + 0.5));
            if (emitter > 0 && (emitter == hit.id || emitter == mirrorId))
                continue;
            bool infinite = uLightAux[i].y > 0.5;
            vec3 lightPos = uLightPos[i].xyz;
            vec3 lightDirIn = infinite ? normalize(lightPos) : normalize(lightPos - center);
            vec3 n = normalize(normalize(fromCenter) + lightDirIn);
            bool solved = true;
            for (int k = 0; k < 8; ++k)
            {
                vec3 q = n * radius;
                vec3 toPoint = fromCenter - q;
                vec3 toLight = infinite ? lightDirIn : (lightPos - center) - q;
                float pointLen = length(toPoint);
                float lightLen = infinite ? 1.0 : length(toLight);
                if (pointLen <= EPS || lightLen <= EPS)
                {
                    solved = false;
                    break;
                }
                toPoint /= pointLen;
                toLight /= lightLen;
                vec3 bisector = toPoint + toLight;
                if (dot(bisector, bisector) <= 1e-6)
                {
                    solved = false;
                    break;
                }
                vec3 next = normalize(bisector);
                if (dot(next, n) > 0.999)
                {
                    n = next;
                    break;
                }
                n = next;
            }
            if (!solved)
                continue;
            vec3 Q = center + n * radius;
            vec3 toPoint = hit.point - Q;
            vec3 incident = infinite ? -lightDirIn : normalize(Q - lightPos);
            float pointDist = length(toPoint);
            if (pointDist <= EPS)
                continue;
            toPoint /= pointDist;
            vec3 reflected = reflect(incident, n);
            if (dot(reflected, toPoint) < 0.995)
                continue;
            if (dot(n, toPoint) <= 0.0 || dot(n, -incident) <= 0.0)
                continue;
            float nDotL = dot(hit.normal, -toPoint);
            if (nDotL <= 0.0)
                continue;
            float cosIn = max(dot(n, -incident), 0.0);
            float attenuation = uLightPos[i].w * cosIn;
            if (!infinite)
                attenuation = uLightPos[i].w * cosIn / (1.0 + uLightColor[i].w * pointDist * pointDist);
            if (uLightSpot[i].w > 0.0)
            {
                vec3 axis = uLightSpot[i].xyz;
                float axisLen = length(axis);
                if (axisLen <= 1e-6)
                    continue;
                axis /= axisLen;
                float cosOuter = cos(uLightSpot[i].w);
                float innerRad = clamp(uLightAux[i].z, 0.0, uLightSpot[i].w);
                float denom = max(cos(innerRad) - cosOuter, 1e-4);
                vec3 towardLight = infinite ? lightDirIn : normalize(lightPos - Q);
                float spot = clamp((dot(axis, -towardLight) - cosOuter) / denom, 0.0, 1.0);
                spot *= spot;
                if (spot <= 0.0)
                    continue;
                attenuation *= spot;
            }
            if (attenuation * reflectW <= 0.002)
                continue;
            if (segmentBlocked(hit.point, Q, hit.id, mirrorId))
                continue;
            vec3 towardLight = infinite ? lightDirIn : normalize(lightPos - Q);
            if (occluded(Q, towardLight, infinite ? Q + towardLight : lightPos, infinite, i, 0, 1, 0.0, mirrorId, emitter))
                continue;
            extra += tint * reflectW * hit.albedo * hit.diffuse * uLightColor[i].rgb * attenuation * nDotL;
        }
    }
)glsl"
R"glsl(
    if (uMirrorCount < 2)
        return extra;
    int bestB = -1;
    float bestFace = 0.05;
    for (int b = 0; b < 4; ++b)
    {
        if (b >= uMirrorCount)
            break;
        int sb = uMirrorIndex[b];
        if (sb < 0 || sb >= uSphereCount)
            continue;
        if (uSphereId[sb] == hit.id)
            continue;
        vec3 toward = uSphereGeom[sb].xyz - hit.point;
        float facing = dot(hit.normal, toward);
        if (facing > bestFace)
        {
            bestFace = facing;
            bestB = b;
        }
    }
    if (bestB < 0)
        return extra;
    int sb = uMirrorIndex[bestB];
    int idB = uSphereId[sb];
    vec3 centerB = uSphereGeom[sb].xyz;
    float radiusB = uSphereGeom[sb].w;
    float reflectB = uSphereMat[sb].w;
    vec3 tintB = uSphereAlbedo[sb].rgb;
    if (radiusB <= 0.0001)
        return extra;
    for (int a = 0; a < 4; ++a)
    {
        if (a >= uMirrorCount || a == bestB)
            continue;
        int sa = uMirrorIndex[a];
        if (sa < 0 || sa >= uSphereCount)
            continue;
        int idA = uSphereId[sa];
        if (idA == hit.id)
            continue;
        vec3 centerA = uSphereGeom[sa].xyz;
        float radiusA = uSphereGeom[sa].w;
        if (radiusA <= 0.0001)
            continue;
        float reflectA = uSphereMat[sa].w;
        if (reflectA * reflectB <= 0.02)
            continue;
        vec3 span = centerA - centerB;
        if (dot(span, span) <= 1e-8)
            continue;
        vec3 nB0 = normalize(normalize(hit.point - centerB) + normalize(span));
        float minDist = length(hit.point - centerB) - radiusB;
        if (minDist < 0.0)
            continue;
        vec3 tintA = uSphereAlbedo[sa].rgb;
        for (int i = 0; i < uLightCount; ++i)
        {
            int emitter = int(floor(uLightAux[i].w + 0.5));
            if (emitter > 0 && (emitter == hit.id || emitter == idA || emitter == idB))
                continue;
            bool infinite = uLightAux[i].y > 0.5;
            vec3 lightPos = uLightPos[i].xyz;
            float brightest = uLightPos[i].w * reflectA * reflectB;
            if (!infinite)
                brightest /= (1.0 + uLightColor[i].w * minDist * minDist);
            if (brightest <= 0.002)
                continue;
            vec3 nB = nB0;
            vec3 Qa = centerA;
            vec3 Qb = centerB;
            vec3 nA = nB;
            vec3 toPoint = vec3(0.0);
            float pointDist = 0.0;
            float alignB = 0.0;
            float cosB = 0.0;
            float cosA = 0.0;
            bool solved = false;
            for (int pass = 0; pass < 2; ++pass)
            {
                vec3 guessB = centerB + nB * radiusB;
                float alignA = 0.0;
                float distA = 0.0;
                vec3 towardB = vec3(0.0);
                if (!bouncePoint(centerA, radiusA, lightPos, infinite, guessB, Qa, nA, towardB, distA, alignA, cosA))
                    break;
                if (!bouncePoint(centerB, radiusB, Qa, false, hit.point, Qb, nB, toPoint, pointDist, alignB, cosB))
                    break;
                solved = alignA >= 0.995 && alignB >= 0.995;
                if (solved)
                    break;
            }
            if (!solved || pointDist <= EPS)
                continue;
            float nDotL = dot(hit.normal, -toPoint);
            if (nDotL <= 0.0 || cosA <= 0.0 || cosB <= 0.0)
                continue;
            float attenuation = uLightPos[i].w * cosA * cosB;
            if (!infinite)
                attenuation = uLightPos[i].w * cosA * cosB / (1.0 + uLightColor[i].w * pointDist * pointDist);
            if (uLightSpot[i].w > 0.0)
            {
                vec3 axis = uLightSpot[i].xyz;
                float axisLen = length(axis);
                if (axisLen <= 1e-6)
                    continue;
                axis /= axisLen;
                float cosOuter = cos(uLightSpot[i].w);
                float denom = max(cos(clamp(uLightAux[i].z, 0.0, uLightSpot[i].w)) - cosOuter, 1e-4);
                vec3 fromLight = infinite ? normalize(lightPos) : normalize(Qa - lightPos);
                float spot = clamp((dot(axis, fromLight) - cosOuter) / denom, 0.0, 1.0);
                if (spot <= 0.0)
                    continue;
                attenuation *= spot * spot;
            }
            if (attenuation * reflectA * reflectB <= 0.002)
                continue;
            if (segmentBlocked(hit.point, Qb, hit.id, idB))
                continue;
            if (segmentBlocked(Qb, Qa, idB, idA))
                continue;
            vec3 towardLight = infinite ? normalize(lightPos) : normalize(lightPos - Qa);
            if (occluded(Qa, towardLight, infinite ? Qa + towardLight : lightPos, infinite, i, 0, 1, 0.0, idA, emitter))
                continue;
            extra += tintA * tintB * reflectA * reflectB * hit.albedo * hit.diffuse * uLightColor[i].rgb * attenuation * nDotL;
        }
    }
    return extra;
}

)glsl"
R"glsl(
vec3 traceRay(vec3 origin, vec3 dir, int lightSample, int cameraSamples)
{
    vec3 result = vec3(0.0);
    vec3 jobOrigin[8];
    vec3 jobDir[8];
    vec3 jobWeight[8];
    int jobDepth[8];
    int jobMirror[8];
    int count = 1;
    int jobCap = uJobStackLimit;
    if (jobCap < 1)
        jobCap = 1;
    if (jobCap > 8)
        jobCap = 8;
    jobOrigin[0] = origin;
    jobDir[0] = dir;
    jobWeight[0] = vec3(1.0);
    jobDepth[0] = 0;
    jobMirror[0] = 0;
    int traceLimit = uHasGlass == 1 ? uTraceLimit : uDepth + 1;
    if (traceLimit < 1)
        traceLimit = 1;
    if (traceLimit > uTraceLimit)
        traceLimit = uTraceLimit;
    if (traceLimit > 24)
        traceLimit = 24;
    for (int step = 0; step < traceLimit; ++step)
    {
        if (count <= 0)
            break;
        count -= 1;
        vec3 ro = jobOrigin[count];
        vec3 rd = jobDir[count];
        vec3 rw = jobWeight[count];
        int depth = jobDepth[count];
        int mirror = jobMirror[count];
        Hit hit = closest(ro, rd, EPS, 1e20);
        if (depth == 0)
            applyRasterMesh(hit, ro, rd);
        else if (mirror == 1)
            traceMesh(hit, ro, rd, 1e20, false);
        if (!hit.ok)
        {
            result += rw * background(rd);
            continue;
        }
        shadeNormal(hit);

        bool glass = hit.transmission > 0.001;
        float reflectW = 0.0;
        float transmitW = 0.0;
        vec3 reflectTint = hit.albedo;
        vec3 transmitDir = rd;
        float diffuseScale = 1.0;
        if (glass && depth < uDepth)
        {
            float n1 = hit.front > 0.5 ? 1.0 : hit.ior;
            float n2 = hit.front > 0.5 ? hit.ior : 1.0;
            float cosTheta = clamp(dot(-rd, hit.normal), 0.0, 1.0);
            float fresnel = schlick(cosTheta, n1, n2);
            bool tir = false;
            transmitDir = refractRay(rd, hit.normal, n1 / max(n2, 0.0001), tir);
            if (tir)
                fresnel = 1.0;
            reflectW = fresnel;
            transmitW = tir ? 0.0 : (1.0 - fresnel) * hit.transmission;
            reflectTint = vec3(1.0);
            diffuseScale = 1.0 - hit.transmission;
        }
        else if (!glass)
        {
            reflectW = depth < uDepth ? clamp(hit.reflectivity, 0.0, 1.0) : 0.0;
        }

        float keep = glass ? 1.0 : (1.0 - reflectW);
        vec3 bounced = depth == 0 ? mirrorBounce(hit) : vec3(0.0);
        vec3 shaded = (lightSurface(rd, hit, diffuseScale, lightSample, cameraSamples) + bounced) * keep;
        if (uFogDensity > 0.0)
        {
            float fog = 1.0 - exp(-uFogDensity * hit.t);
            shaded = mix(shaded, uFogColor, clamp(fog, 0.0, 1.0));
        }
        result += rw * shaded;
        if (hit.id == uSelected)
        {
            float facing = max(dot(hit.normal, -rd), 0.0);
            result += rw * vec3(0.25, 0.55, 1.0) * pow(1.0 - facing, 2.0);
        }
        if (reflectW > 0.0 && count < jobCap)
        {
            jobOrigin[count] = hit.point + hit.normal * EPS;
            jobDir[count] = normalize(rd - hit.normal * (2.0 * dot(rd, hit.normal)));
            jobWeight[count] = rw * reflectW * reflectTint;
            jobDepth[count] = depth + 1;
            jobMirror[count] = reflectW > 0.35 ? 1 : 0;
            count += 1;
        }
        if (transmitW > 0.0 && count < jobCap)
        {
            jobOrigin[count] = hit.point - hit.normal * EPS;
            jobDir[count] = transmitDir;
            jobWeight[count] = rw * transmitW * hit.albedo;
            jobDepth[count] = depth + 1;
            jobMirror[count] = 1;
            count += 1;
        }
    }
    return result;
}

void main()
{
    int samples = uSamples;
    if (samples < 1)
        samples = 1;
    if (samples > 4)
        samples = 4;
    int total = samples * samples;
    int batch = uSampleBatch;
    if (batch < 1)
        batch = total;
    int begin = uSampleOffset;
    if (begin < 0)
        begin = 0;
    if (begin >= total)
        begin = 0;
    if (begin + batch > total)
        batch = total - begin;
    float pixelX = gl_FragCoord.x - 0.5;
    float pixelY = gl_FragCoord.y - 0.5;
    vec3 color = vec3(0.0);
    int n = 0;
    int sampleIndex = begin;
    while (n < batch)
    {
        int sx = sampleIndex - (sampleIndex / samples) * samples;
        int sy = sampleIndex / samples;
        float s = (pixelX + (float(sx) + 0.5) / float(samples)) / float(uWidth);
        float t = 1.0 - (pixelY + (float(sy) + 0.5) / float(samples)) / float(uHeight);
        vec3 dir = normalize(uLowerLeft + s * uHorizontal + t * uVertical - uOrigin);
        vec3 origin = uOrigin;
        if (uAperture > 0.0001 && total > 1)
        {
            float radius = sqrt((float(sampleIndex) + 0.5) / float(total));
            float theta = float(sampleIndex) * 2.39996323;
            vec3 lens = uOrigin + uCamRight * (cos(theta) * radius * uAperture) + uCamUp * (sin(theta) * radius * uAperture);
            float focus = uFocus > 0.0001 ? uFocus : 10.0;
            vec3 focal = uOrigin + dir * (focus / max(dot(dir, uView), 0.05));
            origin = lens;
            dir = normalize(focal - lens);
        }
        color += traceRay(origin, dir, sampleIndex, samples);
        sampleIndex += 1;
        n += 1;
    }
    color /= float(batch);
    color = max(color, vec3(0.0));
    if (uParticleCount > 0)
    {
        float s = gl_FragCoord.x / float(uWidth);
        float t = 1.0 - gl_FragCoord.y / float(uHeight);
        vec3 viewDir = normalize(uLowerLeft + s * uHorizontal + t * uVertical - uOrigin);
        vec3 right = normalize(uCamRight);
        vec3 up = normalize(uCamUp);
        vec3 forward = normalize(cross(right, up));
        int particles = uParticleCount > 16 ? 16 : uParticleCount;
        for (int i = 0; i < 16; ++i)
        {
            if (i >= particles)
                break;
            vec3 center = uParticlePos[i].xyz;
            float size = max(uParticlePos[i].w, 0.001);
            float denom = dot(viewDir, forward);
            if (abs(denom) < 1e-5)
                continue;
            float particleT = dot(center - uOrigin, forward) / denom;
            if (particleT < 0.02)
                continue;
            vec3 rel = (uOrigin + viewDir * particleT) - center;
            float alongRight = dot(rel, right);
            float alongUp = dot(rel, up);
            float dist2 = alongRight * alongRight + alongUp * alongUp;
            if (dist2 > size * size)
                continue;
            float fade = 1.0 - sqrt(dist2) / size;
            color += uParticleColor[i].rgb * fade * uParticleColor[i].a;
        }
    }
    if (uLinear == 0)
    {
        color = color / (vec3(1.0) + color);
        color = pow(color, vec3(1.0 / 2.2));
    }
    fragColor = vec4(color, 1.0);
}
)glsl";
