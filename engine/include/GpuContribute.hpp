#pragma once

#include "Object.hpp"
#include "Vec3.hpp"

namespace gpu_detail
{

// Render-side dispatch. Core objects have no GPU virtual; tests do not link render.
struct GpuContribute
{
    virtual ~GpuContribute() = default;
    virtual void sphere(const Object &object, const Vec3 &center, double worldRadius) = 0;
    virtual void plane(const Object &object, const Vec3 &point, const Vec3 &normal, bool checker, const Vec3 &checkerAlbedo, double checkerScale) = 0;
    virtual void mesh(const Object &object) = 0;
};

inline void contribute(const Object &object, GpuContribute &sink)
{
    if (object.isSphere())
    {
        sink.sphere(object, object.displayWorldPosition(), object.displayWorldRadius());
        return;
    }
    if (object.isPlane())
    {
        sink.plane(object, object.displayWorldPosition(), object.displayWorldNormal(), object.checker(), object.checkerAlbedo(), object.checkerScale());
        return;
    }
    if (object.isMesh())
        sink.mesh(object);
}

} // namespace gpu_detail
