#pragma once

#include "Vec3.hpp"

struct Ray
{
    Vec3 origin;
    Vec3 direction;

    Ray() = default;
    Ray(const Vec3 &origin, const Vec3 &direction) : origin(origin), direction(direction) {}

    Vec3 at(double t) const
    {
        return origin + direction * t;
    }
};
