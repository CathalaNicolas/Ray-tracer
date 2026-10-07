#include "GpuLights.hpp"

#include "Material.hpp"

#include <algorithm>

namespace
{

double lightScore(const Vec3 &origin, const Vec3 &position, double intensity, double falloff, bool directional)
{
    if (directional)
        return 1e12 + intensity;
    const Vec3 delta = position - origin;
    return intensity / (1.0 + falloff * dot(delta, delta));
}

}

std::vector<GpuLightPick> rankGpuLights(const Scene &scene, const Vec3 &origin, int maxLights)
{
    struct Ranked
    {
        GpuLightPick pick;
        double score = 0;
        int order = 0;
    };
    std::vector<Ranked> ranked;
    int order = 0;
    for (std::size_t index = 0; index < scene.lights().size(); ++index)
    {
        const PointLight &light = scene.lights()[index];
        ranked.push_back(Ranked{GpuLightPick{false, index}, lightScore(origin, light.position, light.intensity, light.falloff, light.directional), order++});
    }
    const auto &objects = scene.objects();
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        const Material material = objects[index]->material();
        if (material.emission <= 0.01)
            continue;
        ranked.push_back(Ranked{GpuLightPick{true, index}, lightScore(origin, objects[index]->displayWorldPosition(), material.emission, 0.2, false), order++});
    }
    const int limit = maxLights > 0 ? maxLights : 0;
    if (static_cast<int>(ranked.size()) <= limit)
    {
        std::vector<GpuLightPick> picked;
        for (const Ranked &entry : ranked)
            picked.push_back(entry.pick);
        return picked;
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &left, const Ranked &right) {
        if (left.score != right.score)
            return left.score > right.score;
        return left.order < right.order;
    });
    std::vector<GpuLightPick> picked;
    for (const Ranked &entry : ranked)
    {
        if (static_cast<int>(picked.size()) >= limit)
            break;
        picked.push_back(entry.pick);
    }
    return picked;
}
