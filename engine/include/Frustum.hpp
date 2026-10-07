#pragma once

#include "Camera.hpp"
#include "Object.hpp"
#include "Vec3.hpp"

#include <cmath>

constexpr double kGpuClipNear = 0.02;
constexpr double kGpuClipFarDefault = 2000;

struct Frustum
{
    Vec3 normal[6];
    double offset[6];
};

void fillPinholeClipMatrix(const Camera &camera, double nearPlane, double farPlane, float out[16]);
Frustum frustumFromClip(const float clip[16]);
Frustum frustumFromCamera(const Camera &camera, double nearPlane, double farPlane);

bool frustumAabbOutside(const Frustum &frustum, const Vec3 &boundsMin, const Vec3 &boundsMax);
bool frustumSphereOutside(const Frustum &frustum, const Vec3 &center, double radius);
bool aabbBeyondDistance(const Vec3 &boundsMin, const Vec3 &boundsMax, const Vec3 &origin, double distance);
bool sphereBeyondDistance(const Vec3 &center, double radius, const Vec3 &origin, double distance);

void meshWorldAabb(const Object &meshObject, Vec3 &boundsMin, Vec3 &boundsMax);
