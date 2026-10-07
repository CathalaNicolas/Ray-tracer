#pragma once

#include "EntityId.hpp"
#include "Vec3.hpp"

#include <string>
#include <vector>

class Scene;
class Camera;

struct PointShadowFit
{
    Vec3 target;
    Vec3 up{0, 1, 0};
    double fov = 0.5;
    double nearPlane = 0.05;
    double farPlane = 10;
    int nearestMesh = -1;
    bool fromCorners = false;
};

PointShadowFit fitPointShadow(const Vec3 &eye, const Vec3 &center, double radius, const std::vector<float> &meshMin, const std::vector<float> &meshMax, int meshCount);

struct BounceRay
{
    Vec3 from;
    Vec3 to;
    bool kept = false;
};

// Same test as the GPU mirror bounce. Fills the dump text and one segment per light-to-mirror and mirror-to-receiver leg.
void mirrorBounceDebug(const Scene &scene, std::string &text, std::vector<BounceRay> &rays);

std::string sceneDebugText(const Scene &scene, const Camera &camera, const Vec3 &lookFrom, const Vec3 &lookAt, double fovDegrees, double aperture, double focusDistance, int width, int height, int samples, int depth, EntityId selectedObject, int selectedLight);
