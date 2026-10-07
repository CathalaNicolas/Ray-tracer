#pragma once

#include "Vec3.hpp"

struct Hit
{
    bool hit = false;
    Vec3 normal;
    Vec3 point;
    float penetration = 0;
};
