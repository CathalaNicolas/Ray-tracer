#include "GfxQuality.hpp"

#include <algorithm>

GfxQuality GfxQuality::fromSettings(const EngineSettings &settings)
{
    GfxQuality q;
    q.viewDistance = static_cast<float>(settings.viewDistance > 0 ? settings.viewDistance : 200.0);
    q.shadowMapSize = settings.shadowMapSize > 0 ? settings.shadowMapSize : 1024;
    q.particleDensity = static_cast<float>(settings.particleDensity > 0 ? settings.particleDensity : 1.0);
    q.shadowCascades = 3;
    q.bloom = true;
    q.atmosphericSky = true;
    return q;
}

void GfxQuality::applyToSettings(EngineSettings &settings) const
{
    settings.viewDistance = viewDistance;
    settings.shadowMapSize = shadowMapSize;
    settings.particleDensity = particleDensity;
}
