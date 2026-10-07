#pragma once

#include "Vec3.hpp"

#include <string>

struct PointLight
{
    std::string name;
    Vec3 position;
    Vec3 color{1, 1, 1};
    double intensity = 1;
    double falloff = 0.02;
    double radius = 0;
    bool directional = false;
    Vec3 spotDirection{0, -1, 0};
    double spotOuter = 0;
    double spotInner = 0;

    PointLight() = default;
    PointLight(const Vec3 &position, const Vec3 &color, double intensity, double falloff = 0.02)
        : position(position), color(color), intensity(intensity), falloff(falloff)
    {
    }
};
