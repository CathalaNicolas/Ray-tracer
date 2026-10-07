#pragma once

#include "Camera.hpp"
#include "Light.hpp"

#include <cstdint>
#include <vector>

struct GfxClusterLight
{
    float position[3];
    float intensity;
    float color[3];
    float falloff;
    float direction[3];
    float radius;
    float spotOuter;
    float spotInner;
    std::uint32_t flags; // 1 = directional, 2 = spot
    std::uint32_t pad;
};

struct GfxClusterGrid
{
    static constexpr int kSizeX = 16;
    static constexpr int kSizeY = 9;
    static constexpr int kSizeZ = 24;
    static constexpr int kTileCount = kSizeX * kSizeY * kSizeZ;
    static constexpr int kMaxLightsPerTile = 64;

    // For each tile: offset into indices, then count.
    std::vector<std::uint32_t> tileOffsets;
    std::vector<std::uint32_t> tileCounts;
    std::vector<std::uint32_t> indices;
    std::vector<GfxClusterLight> lights;
};

// CPU clustered light assignment for the Diligent forward path.
// Replaces rank-to-8 for the primary view.
void gfxBuildLightClusters(const Camera &camera, int width, int height,
    const std::vector<PointLight> &lights, GfxClusterGrid &out);
