#include "GpuShaders.hpp"

#include "GpuLimits.hpp"

const char *kVertexShader = R"glsl(
#version 330 core
void main()
{
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char *kMeshVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aMesh;
uniform mat4 uClip;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
flat out float vMesh;
void main()
{
    vWorld = aPos;
    vNormal = aNormal;
    vUv = aUv;
    vMesh = aMesh;
    gl_Position = uClip * vec4(aPos, 1.0);
}
)glsl";

const char *kMeshFragmentShader = R"glsl(
#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
flat in float vMesh;
layout(location = 0) out vec4 outPos;
layout(location = 1) out vec4 outNorm;
layout(location = 2) out vec4 outUv;
void main()
{
    outPos = vec4(vWorld, vMesh);
    float nLen = length(vNormal);
    vec3 n = nLen > 1e-5 ? vNormal / nLen : vec3(0.0, 1.0, 0.0);
    vec3 dp1 = dFdx(vWorld);
    vec3 dp2 = dFdy(vWorld);
    vec2 du1 = dFdx(vUv);
    vec2 du2 = dFdy(vUv);
    float det = du1.x * du2.y - du2.x * du1.y;
    vec3 tangent = abs(det) > 1e-8 ? (dp1 * du2.y - dp2 * du1.y) / det : vec3(0.0);
    float tLen = length(tangent);
    tangent = tLen > 1e-5 ? tangent / tLen : vec3(0.0);
    outNorm = vec4(n, tangent.z);
    outUv = vec4(vUv, tangent.xy);
}
)glsl";

const char *kShadowVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aMesh;
uniform mat4 uClip;
uniform vec3 uLightPos;
uniform int uParaboloid;
out vec3 vWorld;
flat out float vMesh;
void main()
{
    vWorld = aPos;
    vMesh = aMesh;
    if (uParaboloid == 0)
    {
        gl_Position = uClip * vec4(aPos, 1.0);
        return;
    }
    vec3 side = uClip[0].xyz;
    vec3 up = uClip[1].xyz;
    vec3 fwd = uClip[2].xyz;
    float farP = max(uClip[3].w, 0.05);
    vec3 delta = aPos - uLightPos;
    float dist = length(delta);
    vec3 dir = dist > 1e-5 ? delta / dist : fwd;
    if (dot(dir, fwd) * float(uParaboloid) <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec3 hemi = uParaboloid < 0 ? -dir : dir;
    float denom = max(dot(hemi, fwd) + 1.0, 1e-4);
    vec2 p = vec2(dot(hemi, side), dot(hemi, up)) / denom;
    float ndcZ = clamp(dist / farP, 0.0, 1.0) * 2.0 - 1.0;
    gl_Position = vec4(p, ndcZ, 1.0);
}
)glsl";

const char *kShadowFragmentShader = R"glsl(
#version 330 core
in vec3 vWorld;
flat in float vMesh;
uniform vec3 uLightPos;
uniform vec3 uLightDir;
uniform int uDirectional;
uniform float uSkipMesh;
out float fragMetric;
void main()
{
    if (uSkipMesh >= 0.0 && abs(vMesh - uSkipMesh) < 0.5)
        discard;
    fragMetric = uDirectional == 1 ? dot(vWorld, -uLightDir) : distance(vWorld, uLightPos);
}
)glsl";

const char *kBlendFragmentShader = R"glsl(
#version 330 core
uniform sampler2D uSample;
uniform sampler2D uAccum;
uniform float uPrevious;
out vec4 fragColor;
void main()
{
    vec3 sampleColor = texelFetch(uSample, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 accumColor = texelFetch(uAccum, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 color = uPrevious <= 0.0 ? sampleColor : (accumColor * uPrevious + sampleColor) / (uPrevious + 1.0);
    fragColor = vec4(color, 1.0);
}
)glsl";

const char *kResolveFragmentShader = R"glsl(
#version 330 core
uniform sampler2D uAccum;
uniform int uLinear;
out vec4 fragColor;
void main()
{
    vec3 color = max(texelFetch(uAccum, ivec2(gl_FragCoord.xy), 0).rgb, vec3(0.0));
    if (uLinear == 0)
    {
        color = color / (vec3(1.0) + color);
        color = pow(color, vec3(1.0 / 2.2));
    }
    fragColor = vec4(color, 1.0);
}
)glsl";

const char *kBloomFragmentShader = R"glsl(
#version 330 core
uniform sampler2D uColor;
out vec4 fragColor;
uniform float uBloomThreshold;
uniform float uBloomStrength;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(uColor, pixel, 0).rgb;
    vec3 glow = vec3(0.0);
    float weight = 0.0;
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            vec3 sampleColor = texelFetch(uColor, pixel + ivec2(x, y) * 2, 0).rgb;
            float luma = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
            float bright = max(luma - uBloomThreshold, 0.0);
            float w = exp(-float(x * x + y * y) * 0.15) * bright;
            glow += sampleColor * w;
            weight += w;
        }
    }
    if (weight > 0.0001)
        glow /= weight;
    fragColor = vec4(min(color + glow * uBloomStrength, vec3(1.0)), 1.0);
}
)glsl";

const char *kCopyFragmentShader = R"glsl(
#version 330 core
uniform sampler2D uColor;
out vec4 fragColor;
void main()
{
    fragColor = vec4(texelFetch(uColor, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
}
)glsl";


std::string shaderWithLimits(std::string source)
{
    auto replace = [&](const char *token, int value) {
        const std::string needle = token;
        const std::string text = std::to_string(value);
        for (size_t at = 0; (at = source.find(needle, at)) != std::string::npos; at += text.size())
            source.replace(at, needle.size(), text);
    };
    replace("MAX_SPHERES", kGpuMaxSpheres);
    replace("MAX_PLANES", kGpuMaxPlanes);
    replace("MAX_LIGHTS", kGpuMaxLights);
    replace("MAX_MESHES", kGpuMaxMeshes);
    return source;
}

