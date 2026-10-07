#pragma once

#include "GfxQuality.hpp"

#include <memory>
#include <string>

class GfxDevice;

// DiligentFX presentation helpers: cascaded sun shadows, bloom, atmospheric sky.
// Heavy passes live in DiligentFX; this is a thin quality-driven wrapper (M3).
class GfxFx
{
public:
    GfxFx();
    ~GfxFx();

    bool init(GfxDevice &device, const GfxQuality &quality, std::string &error);
    void shutdown();
    void applyQuality(const GfxQuality &quality);

    // Called each frame after opaque draw, before ImGui.
    void beginSky(float sunDir[3], float timeOfDayHours);
    void endPost();

    bool ready() const { return ready_; }
    int shadowCascades() const { return cascades_; }
    int shadowMapSize() const { return shadowMapSize_; }
    bool bloomEnabled() const { return bloom_; }
    bool skyEnabled() const { return sky_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    int cascades_ = 0;
    int shadowMapSize_ = 0;
    bool bloom_ = false;
    bool sky_ = false;
};
