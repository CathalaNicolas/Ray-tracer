#include "GfxCluster.hpp"

#include "Constants.hpp"

#include <algorithm>
#include <cmath>

namespace
{

float clampf(float v, float lo, float hi)
{
    return std::max(lo, std::min(hi, v));
}

} // namespace

void gfxBuildLightClusters(const Camera &camera, int width, int height,
    const std::vector<PointLight> &lights, GfxClusterGrid &out)
{
    out = {};
    out.lights.reserve(lights.size());
    for (const PointLight &src : lights)
    {
        GfxClusterLight light{};
        light.position[0] = static_cast<float>(src.position.x);
        light.position[1] = static_cast<float>(src.position.y);
        light.position[2] = static_cast<float>(src.position.z);
        light.intensity = static_cast<float>(src.intensity);
        light.color[0] = static_cast<float>(src.color.x);
        light.color[1] = static_cast<float>(src.color.y);
        light.color[2] = static_cast<float>(src.color.z);
        light.falloff = static_cast<float>(src.falloff);
        light.direction[0] = static_cast<float>(src.spotDirection.x);
        light.direction[1] = static_cast<float>(src.spotDirection.y);
        light.direction[2] = static_cast<float>(src.spotDirection.z);
        light.radius = static_cast<float>(src.radius > 0 ? src.radius : 8.0);
        light.spotOuter = static_cast<float>(src.spotOuter);
        light.spotInner = static_cast<float>(src.spotInner);
        light.flags = src.directional ? 1u : (src.spotOuter > 0 ? 2u : 0u);
        out.lights.push_back(light);
    }

    const int tiles = GfxClusterGrid::kTileCount;
    out.tileOffsets.assign(static_cast<std::size_t>(tiles), 0);
    out.tileCounts.assign(static_cast<std::size_t>(tiles), 0);
    out.indices.clear();

    if (width < 1 || height < 1 || out.lights.empty())
        return;

    // Camera-relative: assign each local light to a coarse screen/depth tile AABB.
    // Directional lights go into every tile (cheap sun).
    const Vec3 cam = camera.origin();
    const float nearZ = 0.1f;
    const float farZ = 500.f;
    (void)nearZ;
    (void)farZ;

    for (int z = 0; z < GfxClusterGrid::kSizeZ; ++z)
    {
        for (int y = 0; y < GfxClusterGrid::kSizeY; ++y)
        {
            for (int x = 0; x < GfxClusterGrid::kSizeX; ++x)
            {
                const int tile = (z * GfxClusterGrid::kSizeY + y) * GfxClusterGrid::kSizeX + x;
                const std::uint32_t offset = static_cast<std::uint32_t>(out.indices.size());
                std::uint32_t count = 0;
                for (std::uint32_t li = 0; li < out.lights.size(); ++li)
                {
                    const GfxClusterLight &L = out.lights[li];
                    if ((L.flags & 1u) != 0)
                    {
                        out.indices.push_back(li);
                        ++count;
                        continue;
                    }
                    // Project light into cluster space using a simple normalized offset from camera.
                    const float dx = L.position[0] - static_cast<float>(cam.x);
                    const float dy = L.position[1] - static_cast<float>(cam.y);
                    const float dz = L.position[2] - static_cast<float>(cam.z);
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float nx = clampf(0.5f + dx / (80.f + L.radius), 0.f, 0.999f);
                    const float ny = clampf(0.5f + dy / (80.f + L.radius), 0.f, 0.999f);
                    const float nz = clampf(dist / 200.f, 0.f, 0.999f);
                    const int cx = static_cast<int>(nx * GfxClusterGrid::kSizeX);
                    const int cy = static_cast<int>(ny * GfxClusterGrid::kSizeY);
                    const int cz = static_cast<int>(nz * GfxClusterGrid::kSizeZ);
                    const int reach = std::max(1, static_cast<int>(L.radius / 16.f));
                    if (std::abs(cx - x) <= reach && std::abs(cy - y) <= reach && std::abs(cz - z) <= reach)
                    {
                        if (count < static_cast<std::uint32_t>(GfxClusterGrid::kMaxLightsPerTile))
                        {
                            out.indices.push_back(li);
                            ++count;
                        }
                    }
                }
                out.tileOffsets[static_cast<std::size_t>(tile)] = offset;
                out.tileCounts[static_cast<std::size_t>(tile)] = count;
            }
        }
    }
    (void)camera;
}
