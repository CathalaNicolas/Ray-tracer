#pragma once

#include "Camera.hpp"
#include "Scene.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxDevice;

// Opaque mesh forward view on Diligent. Instance/vertex buffers grow at runtime
// (no GpuLimits mesh caps on this path).
class GfxView
{
public:
    GfxView();
    ~GfxView();

    bool init(GfxDevice &device, std::string &error);
    void shutdown();

    // Rebuild GPU buffers from the scene (meshes + materials + lights).
    void upload(const Scene &scene, const Camera &camera);

    // Draw opaque meshes into the current Diligent frame (after beginFrame/clear).
    void draw(const Scene &scene, const Camera &camera);

    // Runtime-sized counts (for HUD / debug).
    int meshInstances() const { return meshInstances_; }
    int uniqueGeometries() const { return uniqueGeometries_; }
    int lightCount() const { return lightCount_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int meshInstances_ = 0;
    int uniqueGeometries_ = 0;
    int lightCount_ = 0;
};
