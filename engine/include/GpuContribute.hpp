#pragma once

#include "Vec3.hpp"

class Hittable;
class Mesh;

namespace gpu_detail
{

// Shapes call one of these instead of the upload loop casting.
struct GpuContribute
{
    virtual ~GpuContribute() = default;
    virtual void sphere(const Hittable &object, const Vec3 &center, double worldRadius) = 0;
    virtual void plane(const Hittable &object, const Vec3 &point, const Vec3 &normal, bool checker, const Vec3 &checkerAlbedo, double checkerScale) = 0;
    virtual void mesh(const Mesh &object) = 0;
};

}
