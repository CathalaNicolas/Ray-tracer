#pragma once

#include <ostream>
#include <string>

// Fixed GLSL array sizes in GpuShaderTrace.cpp. Settings clamp to these.
constexpr int kGlslJobStackMax = 8;
constexpr int kGlslTraceLimitMax = 24;
constexpr int kGlslMeshStackMax = 24;

// Runtime tunables. Defaults match the values the engine shipped with.
// raytracer-settings.txt and the Settings menu read and write these.
struct EngineSettings
{
    // Play
    double moveSpeed = 4;
    double gravity = -12;
    double jumpSpeed = 5;
    double groundProbe = 0.08;
    double stepHeight = 0.35;
    double fallY = -3;
    double actionDuration = 0.4;
    double playRayDistance = 3;
    int maxTweens = 8;
    int maxPlayStepsPerFrame = 4;
    double maxPlayFrameDt = 0.25;
    int minSubsteps = 4;
    int maxSubsteps = 32;
    int resolvePasses = 4;
    int maxParticles = 32;
    double soundFar = 16;

    // Render / GPU
    bool mirrorBounces = true;
    double mirrorReflectMin = 0.35;
    int meshTraceLimit = 96;
    // GLSL array sizes are fixed; these clamp the live walk. Defaults match the arrays.
    int jobStackLimit = 8;
    int traceLimit = 24;
    int meshStackLimit = 24;
    double bloomThreshold = 0.88;
    double bloomStrength = 0.85;
    double viewDistance = 2000;
    int shadowMapSize = 1024;
    double particleDensity = 1;

    // Debug overlay
    double frustumNear = 0.35;
    double frustumFar = 8;

    // Editor (also in settings file)
    double snap = 0.5;
};

EngineSettings &engineSettings();

// Parse one "key value" line. Unknown keys are ignored. Returns true if a known key was applied.
bool applyEngineSetting(const std::string &key, const std::string &value);

// Write every engine setting key (not volume, keys, or render size).
void writeEngineSettings(std::ostream &out);
