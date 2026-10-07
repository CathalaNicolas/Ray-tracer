#pragma once

#include "Scene.hpp"

#include <cstddef>
#include <vector>

struct GpuLightPick
{
    bool fromObject = false;
    std::size_t index = 0;
};

std::vector<GpuLightPick> rankGpuLights(const Scene &scene, const Vec3 &origin, int maxLights);
