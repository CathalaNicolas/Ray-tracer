#include "Frustum.hpp"

#include "MeshGeometry.hpp"
#include "Object.hpp"
#include "TransformMath.hpp"

#include <algorithm>

namespace
{

void setPlane(Frustum &frustum, int index, double x, double y, double z, double w)
{
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length <= 1e-12)
    {
        frustum.normal[index] = Vec3(0, 1, 0);
        frustum.offset[index] = 0;
        return;
    }
    frustum.normal[index] = Vec3(x / length, y / length, z / length);
    frustum.offset[index] = w / length;
}

bool planeOutsideAabb(const Vec3 &normal, double offset, const Vec3 &boundsMin, const Vec3 &boundsMax)
{
    const Vec3 positive(
        normal.x >= 0 ? boundsMax.x : boundsMin.x,
        normal.y >= 0 ? boundsMax.y : boundsMin.y,
        normal.z >= 0 ? boundsMax.z : boundsMin.z);
    return dot(normal, positive) + offset < 0;
}

Vec3 closestPointOnAabb(const Vec3 &point, const Vec3 &boundsMin, const Vec3 &boundsMax)
{
    return Vec3(
        std::min(std::max(point.x, boundsMin.x), boundsMax.x),
        std::min(std::max(point.y, boundsMin.y), boundsMax.y),
        std::min(std::max(point.z, boundsMin.z), boundsMax.z));
}

}

void fillPinholeClipMatrix(const Camera &camera, double nearPlane, double farPlane, float out[16])
{
    const Vec3 origin = camera.origin();
    const Vec3 right = camera.rightAxis();
    const Vec3 up = camera.upAxis();
    const Vec3 forward = camera.viewDirection();
    const double halfW = length(camera.horizontal()) * 0.5;
    const double halfH = length(camera.vertical()) * 0.5;
    const double nearP = nearPlane > 1e-5 ? nearPlane : kGpuClipNear;
    double farP = farPlane > nearP ? farPlane : kGpuClipFarDefault;
    const double rl = nearP * 2.0 * halfW;
    const double tb = nearP * 2.0 * halfH;
    const double a = (farP + nearP) / (nearP - farP);
    const double b = (2.0 * farP * nearP) / (nearP - farP);
    const double m00 = nearP * 2.0 / rl;
    const double m11 = nearP * 2.0 / tb;
    auto set = [&](int row, int column, double value) {
        out[column * 4 + row] = static_cast<float>(value);
    };
    for (int i = 0; i < 16; ++i)
        out[i] = 0;
    set(0, 0, right.x * m00);
    set(0, 1, right.y * m00);
    set(0, 2, right.z * m00);
    set(0, 3, -dot(right, origin) * m00);
    set(1, 0, up.x * m11);
    set(1, 1, up.y * m11);
    set(1, 2, up.z * m11);
    set(1, 3, -dot(up, origin) * m11);
    set(2, 0, forward.x * a);
    set(2, 1, forward.y * a);
    set(2, 2, forward.z * a);
    set(2, 3, -dot(forward, origin) * a + b);
    set(3, 0, forward.x);
    set(3, 1, forward.y);
    set(3, 2, forward.z);
    set(3, 3, -dot(forward, origin));
}

Frustum frustumFromClip(const float clip[16])
{
    Frustum frustum;
    auto row = [&](int r, int c) {
        return static_cast<double>(clip[c * 4 + r]);
    };
    setPlane(frustum, 0, row(3, 0) + row(0, 0), row(3, 1) + row(0, 1), row(3, 2) + row(0, 2), row(3, 3) + row(0, 3));
    setPlane(frustum, 1, row(3, 0) - row(0, 0), row(3, 1) - row(0, 1), row(3, 2) - row(0, 2), row(3, 3) - row(0, 3));
    setPlane(frustum, 2, row(3, 0) + row(1, 0), row(3, 1) + row(1, 1), row(3, 2) + row(1, 2), row(3, 3) + row(1, 3));
    setPlane(frustum, 3, row(3, 0) - row(1, 0), row(3, 1) - row(1, 1), row(3, 2) - row(1, 2), row(3, 3) - row(1, 3));
    setPlane(frustum, 4, row(3, 0) + row(2, 0), row(3, 1) + row(2, 1), row(3, 2) + row(2, 2), row(3, 3) + row(2, 3));
    setPlane(frustum, 5, row(3, 0) - row(2, 0), row(3, 1) - row(2, 1), row(3, 2) - row(2, 2), row(3, 3) - row(2, 3));
    return frustum;
}

Frustum frustumFromCamera(const Camera &camera, double nearPlane, double farPlane)
{
    const Vec3 origin = camera.origin();
    const Vec3 forward = camera.viewDirection();
    const Vec3 right = camera.rightAxis();
    const Vec3 up = camera.upAxis();
    const double halfW = std::max(length(camera.horizontal()) * 0.5, 1e-6);
    const double halfH = std::max(length(camera.vertical()) * 0.5, 1e-6);
    const double nearP = nearPlane > 1e-5 ? nearPlane : kGpuClipNear;
    const double farP = farPlane > nearP ? farPlane : kGpuClipFarDefault;
    const Vec3 inside = origin + forward * ((nearP + farP) * 0.5);
    auto corner = [&](double depth, double sx, double sy) {
        return origin + forward * depth + right * (halfW * depth * sx) + up * (halfH * depth * sy);
    };
    const Vec3 nbl = corner(nearP, -1, -1);
    const Vec3 nbr = corner(nearP, 1, -1);
    const Vec3 ntl = corner(nearP, -1, 1);
    const Vec3 ntr = corner(nearP, 1, 1);
    const Vec3 fbl = corner(farP, -1, -1);
    const Vec3 fbr = corner(farP, 1, -1);
    const Vec3 ftl = corner(farP, -1, 1);
    Frustum frustum;
    auto setFacingIn = [&](int index, const Vec3 &a, const Vec3 &b, const Vec3 &c) {
        Vec3 normal = cross(b - a, c - a);
        const double len = length(normal);
        if (len <= 1e-12)
        {
            frustum.normal[index] = Vec3(0, 1, 0);
            frustum.offset[index] = 0;
            return;
        }
        normal = normal * (1.0 / len);
        double offset = -dot(normal, a);
        if (dot(normal, inside) + offset < 0)
        {
            normal = normal * -1.0;
            offset = -offset;
        }
        frustum.normal[index] = normal;
        frustum.offset[index] = offset;
    };
    setFacingIn(0, nbl, ntl, ftl);
    setFacingIn(1, nbr, fbr, ntr);
    setFacingIn(2, nbl, fbl, nbr);
    setFacingIn(3, ntl, ntr, ftl);
    setFacingIn(4, nbl, nbr, ntr);
    setFacingIn(5, fbl, ftl, fbr);
    return frustum;
}

bool frustumAabbOutside(const Frustum &frustum, const Vec3 &boundsMin, const Vec3 &boundsMax)
{
    for (int plane = 0; plane < 6; ++plane)
    {
        if (planeOutsideAabb(frustum.normal[plane], frustum.offset[plane], boundsMin, boundsMax))
            return true;
    }
    return false;
}

bool frustumSphereOutside(const Frustum &frustum, const Vec3 &center, double radius)
{
    for (int plane = 0; plane < 6; ++plane)
    {
        if (dot(frustum.normal[plane], center) + frustum.offset[plane] < -radius)
            return true;
    }
    return false;
}

bool aabbBeyondDistance(const Vec3 &boundsMin, const Vec3 &boundsMax, const Vec3 &origin, double distance)
{
    const Vec3 closest = closestPointOnAabb(origin, boundsMin, boundsMax);
    const Vec3 delta = closest - origin;
    return dot(delta, delta) > distance * distance;
}

bool sphereBeyondDistance(const Vec3 &center, double radius, const Vec3 &origin, double distance)
{
    const double reach = distance + radius;
    const Vec3 delta = center - origin;
    return dot(delta, delta) > reach * reach;
}

void meshWorldAabb(const Object &meshObject, Vec3 &boundsMin, Vec3 &boundsMax)
{
    const MeshGeometry *geometry = meshObject.geometry().get();
    boundsMin = Vec3(1e30, 1e30, 1e30);
    boundsMax = Vec3(-1e30, -1e30, -1e30);
    if (geometry == nullptr)
        return;
    const glm::dmat4 world = meshObject.displayWorldMatrix();
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec3 local(
            (corner & 1) != 0 ? geometry->boundsMax.x : geometry->boundsMin.x,
            (corner & 2) != 0 ? geometry->boundsMax.y : geometry->boundsMin.y,
            (corner & 4) != 0 ? geometry->boundsMax.z : geometry->boundsMin.z);
        const Vec3 placed = transformPoint(world, local);
        boundsMin.x = std::min(boundsMin.x, placed.x);
        boundsMin.y = std::min(boundsMin.y, placed.y);
        boundsMin.z = std::min(boundsMin.z, placed.z);
        boundsMax.x = std::max(boundsMax.x, placed.x);
        boundsMax.y = std::max(boundsMax.y, placed.y);
        boundsMax.z = std::max(boundsMax.z, placed.z);
    }
    boundsMin = boundsMin - Vec3(1e-3, 1e-3, 1e-3);
    boundsMax = boundsMax + Vec3(1e-3, 1e-3, 1e-3);
}
