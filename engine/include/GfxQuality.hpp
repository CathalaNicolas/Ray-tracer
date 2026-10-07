#pragma once

#include "EngineSettings.hpp"

// Graphics quality for the Diligent path (view distance, shadows, density).
struct GfxQuality
{
    float viewDistance = 200.f;
    int shadowMapSize = 1024;
    int shadowCascades = 3;
    float particleDensity = 1.f;
    bool bloom = true;
    bool ssao = false;
    bool taa = false;
    bool atmosphericSky = true;

    static GfxQuality fromSettings(const EngineSettings &settings);
    void applyToSettings(EngineSettings &settings) const;
};
