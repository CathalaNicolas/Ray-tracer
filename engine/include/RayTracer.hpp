#pragma once

#include "Camera.hpp"
#include "Image.hpp"
#include "Scene.hpp"

class RayTracer
{
public:
    int maxDepth = 4;
    int sampleGrid = 2;
    int selectedObject = -1;

    Vec3 trace(const Ray &ray, const Scene &scene, int depth) const;
    void render(const Scene &scene, const Camera &camera, Image &image) const;
};
