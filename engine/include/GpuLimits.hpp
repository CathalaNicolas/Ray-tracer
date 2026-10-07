#pragma once

#include "EngineSettings.hpp"
#include "GpuContribute.hpp"
#include "Mesh.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

constexpr int kGpuMaxSpheres = 64;
constexpr int kGpuMaxPlanes = 16;
constexpr int kGpuMaxLights = 8;
constexpr int kGpuMaxMeshes = 256;
constexpr int kMeshRecordRows = 9;
constexpr int kGpuMaxTriangles = 262144;
constexpr int kGpuMaxTextures = 16;

struct GpuSceneLimits
{
    int spheres = 0;
    int planes = 0;
    int lights = 0;
    int meshes = 0;
    int triangles = 0;
    int textures = 0;
    int other = 0;
    int maxBvhDepth = 0;
    bool hasGlass = false;

    std::string warning() const;
    std::string stackWarning(int bounceDepth) const;
};

inline int bvhTreeDepth(const std::vector<BvhNode> &nodes, int index)
{
    if (index < 0 || index >= static_cast<int>(nodes.size()))
        return 0;
    const BvhNode &node = nodes[static_cast<size_t>(index)];
    if (node.right < 0)
        return 1;
    return 1 + std::max(bvhTreeDepth(nodes, node.left), bvhTreeDepth(nodes, node.right));
}

inline GpuSceneLimits gpuSceneLimits(const Scene &scene)
{
    struct CountSink : gpu_detail::GpuContribute
    {
        GpuSceneLimits &limits;
        std::unordered_set<const MeshGeometry *> geometries;

        explicit CountSink(GpuSceneLimits &limits) : limits(limits) {}

        void sphere(const Hittable &object, const Vec3 &, double) override
        {
            ++limits.spheres;
            if (object.material().transmission > 0.001)
                limits.hasGlass = true;
        }
        void plane(const Hittable &object, const Vec3 &, const Vec3 &, bool, const Vec3 &, double) override
        {
            ++limits.planes;
            if (object.material().transmission > 0.001)
                limits.hasGlass = true;
        }
        void mesh(const Mesh &object) override
        {
            ++limits.meshes;
            if (object.material().transmission > 0.001)
                limits.hasGlass = true;
            const MeshGeometry *geometry = object.geometry().get();
            if (geometry != nullptr && geometries.insert(geometry).second)
            {
                limits.triangles += static_cast<int>(geometry->triangles.size());
                limits.maxBvhDepth = std::max(limits.maxBvhDepth, bvhTreeDepth(geometry->nodes, geometry->root));
            }
        }
    };

    GpuSceneLimits limits;
    std::unordered_set<std::string> textures;
    CountSink sink(limits);
    for (const auto &object : scene.objects())
    {
        if (!object->material().albedoMap.empty())
            textures.insert(object->material().albedoMap);
        if (object->material().transmission > 0.001)
            limits.hasGlass = true;
        object->contributeGpu(sink);
    }
    limits.lights = static_cast<int>(scene.lights().size());
    limits.textures = static_cast<int>(textures.size());
    return limits;
}

inline std::string GpuSceneLimits::warning() const
{
    std::string text;
    auto append = [&](int count, int limit, const char *label) {
        if (count <= limit)
            return;
        if (!text.empty())
            text += ", ";
        text += std::to_string(limit) + "/" + std::to_string(count) + " " + label;
    };
    append(spheres, kGpuMaxSpheres, "spheres");
    append(planes, kGpuMaxPlanes, "planes");
    append(lights, kGpuMaxLights, "lights");
    append(meshes, kGpuMaxMeshes, "meshes");
    append(triangles, kGpuMaxTriangles, "triangles");
    append(textures, kGpuMaxTextures, "textures");
    if (other > 0)
    {
        if (!text.empty())
            text += ", ";
        text += std::to_string(other) + " unsupported objects";
    }
    if (text.empty())
        return {};
    return "GPU is drawing " + text + ". The rest are skipped.";
}

inline std::string GpuSceneLimits::stackWarning(int bounceDepth) const
{
    const EngineSettings &settings = engineSettings();
    int jobStack = settings.jobStackLimit;
    if (jobStack > kGlslJobStackMax)
        jobStack = kGlslJobStackMax;
    if (jobStack < 1)
        jobStack = 1;
    int traceCap = settings.traceLimit;
    if (traceCap > kGlslTraceLimitMax)
        traceCap = kGlslTraceLimitMax;
    if (traceCap < 1)
        traceCap = 1;
    int meshStack = settings.meshStackLimit;
    if (meshStack > kGlslMeshStackMax)
        meshStack = kGlslMeshStackMax;
    if (meshStack < 2)
        meshStack = 2;

    std::string text;
    auto append = [&](const std::string &piece) {
        if (piece.empty())
            return;
        if (!text.empty())
            text += "; ";
        text += piece;
    };

    const int meshPushCap = meshStack - 2;
    if (maxBvhDepth > meshPushCap)
        append("mesh stack " + std::to_string(meshStack) + " is below BVH depth " + std::to_string(maxBvhDepth));

    if (hasGlass)
    {
        const int neededJobs = std::min(bounceDepth + 2, kGlslJobStackMax);
        if (jobStack < neededJobs)
            append("job stack " + std::to_string(jobStack) + " may drop glass rays (need ~" + std::to_string(neededJobs) + ")");
        if (traceCap < bounceDepth + 1)
            append("trace limit " + std::to_string(traceCap) + " is below bounce depth " + std::to_string(bounceDepth + 1));
    }

    if (maxBvhDepth > 0 && settings.meshTraceLimit < maxBvhDepth * 2)
        append("mesh_trace_limit " + std::to_string(settings.meshTraceLimit) + " may stop before BVH depth " + std::to_string(maxBvhDepth));

    if (text.empty())
        return {};
    return "GLSL caps: " + text + ".";
}
