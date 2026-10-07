#include "EngineSettings.hpp"

#include <cmath>
#include <cstdlib>
#include <ostream>

namespace
{

EngineSettings gSettings;

bool parseNumber(const std::string &value, double &out)
{
    char *end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str())
        return false;
    out = number;
    return true;
}

int clampInt(double value, int lo, int hi)
{
    int n = static_cast<int>(std::lround(value));
    if (n < lo)
        n = lo;
    if (n > hi)
        n = hi;
    return n;
}

}

EngineSettings &engineSettings()
{
    return gSettings;
}

bool applyEngineSetting(const std::string &key, const std::string &value)
{
    double number = 0;
    if (!parseNumber(value, number))
        return false;
    EngineSettings &s = gSettings;
    if (key == "move_speed")
        s.moveSpeed = number > 0 ? number : s.moveSpeed;
    else if (key == "gravity")
        s.gravity = number;
    else if (key == "jump_speed")
        s.jumpSpeed = number > 0 ? number : s.jumpSpeed;
    else if (key == "ground_probe")
        s.groundProbe = number > 0 ? number : s.groundProbe;
    else if (key == "step_height")
        s.stepHeight = number > 0 ? number : s.stepHeight;
    else if (key == "fall_y")
        s.fallY = number;
    else if (key == "action_duration")
        s.actionDuration = number > 0 ? number : s.actionDuration;
    else if (key == "play_ray")
        s.playRayDistance = number > 0 ? number : s.playRayDistance;
    else if (key == "max_tweens")
        s.maxTweens = clampInt(number, 1, 32);
    else if (key == "max_play_steps")
        s.maxPlayStepsPerFrame = clampInt(number, 1, 16);
    else if (key == "max_frame_dt")
        s.maxPlayFrameDt = number > 0 ? number : s.maxPlayFrameDt;
    else if (key == "min_substeps")
        s.minSubsteps = clampInt(number, 1, 64);
    else if (key == "max_substeps")
        s.maxSubsteps = clampInt(number, 1, 128);
    else if (key == "resolve_passes")
        s.resolvePasses = clampInt(number, 1, 16);
    else if (key == "max_particles")
        s.maxParticles = clampInt(number, 1, 256);
    else if (key == "sound_far")
        s.soundFar = number > 0 ? number : s.soundFar;
    else if (key == "mirrors")
        s.mirrorBounces = number >= 0.5;
    else if (key == "mirror_reflect")
        s.mirrorReflectMin = number > 0 ? number : s.mirrorReflectMin;
    else if (key == "mesh_trace_limit")
        s.meshTraceLimit = clampInt(number, 8, 512);
    else if (key == "job_stack")
        s.jobStackLimit = clampInt(number, 1, kGlslJobStackMax);
    else if (key == "trace_limit")
        s.traceLimit = clampInt(number, 1, kGlslTraceLimitMax);
    else if (key == "mesh_stack")
        s.meshStackLimit = clampInt(number, 2, kGlslMeshStackMax);
    else if (key == "bloom_threshold")
        s.bloomThreshold = number;
    else if (key == "bloom_strength")
        s.bloomStrength = number >= 0 ? number : s.bloomStrength;
    else if (key == "view_distance")
        s.viewDistance = number > 0 ? number : s.viewDistance;
    else if (key == "shadow_map")
        s.shadowMap = clampInt(number, 256, 4096);
    else if (key == "particle_density")
        s.particleDensity = number > 0 ? number : s.particleDensity;
    else if (key == "frustum_near")
        s.frustumNear = number > 0 ? number : s.frustumNear;
    else if (key == "frustum_far")
        s.frustumFar = number > s.frustumNear ? number : s.frustumFar;
    else if (key == "snap")
        s.snap = number > 0 ? number : s.snap;
    else
        return false;
    return true;
}

void writeEngineSettings(std::ostream &out)
{
    const EngineSettings &s = gSettings;
    out << "move_speed " << s.moveSpeed << '\n';
    out << "gravity " << s.gravity << '\n';
    out << "jump_speed " << s.jumpSpeed << '\n';
    out << "ground_probe " << s.groundProbe << '\n';
    out << "step_height " << s.stepHeight << '\n';
    out << "fall_y " << s.fallY << '\n';
    out << "action_duration " << s.actionDuration << '\n';
    out << "play_ray " << s.playRayDistance << '\n';
    out << "max_tweens " << s.maxTweens << '\n';
    out << "max_play_steps " << s.maxPlayStepsPerFrame << '\n';
    out << "max_frame_dt " << s.maxPlayFrameDt << '\n';
    out << "min_substeps " << s.minSubsteps << '\n';
    out << "max_substeps " << s.maxSubsteps << '\n';
    out << "resolve_passes " << s.resolvePasses << '\n';
    out << "max_particles " << s.maxParticles << '\n';
    out << "sound_far " << s.soundFar << '\n';
    out << "mirrors " << (s.mirrorBounces ? 1 : 0) << '\n';
    out << "mirror_reflect " << s.mirrorReflectMin << '\n';
    out << "mesh_trace_limit " << s.meshTraceLimit << '\n';
    out << "job_stack " << s.jobStackLimit << '\n';
    out << "trace_limit " << s.traceLimit << '\n';
    out << "mesh_stack " << s.meshStackLimit << '\n';
    out << "bloom_threshold " << s.bloomThreshold << '\n';
    out << "bloom_strength " << s.bloomStrength << '\n';
    out << "view_distance " << s.viewDistance << '\n';
    out << "shadow_map " << s.shadowMap << '\n';
    out << "particle_density " << s.particleDensity << '\n';
    out << "frustum_near " << s.frustumNear << '\n';
    out << "frustum_far " << s.frustumFar << '\n';
    out << "snap " << s.snap << '\n';
}
