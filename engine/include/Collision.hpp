#pragma once

#include "Vec3.hpp"

struct MeshGeometry;

bool sphereIntersectsSphere(const Vec3 &centerA, double radiusA, const Vec3 &centerB, double radiusB);
bool sphereIntersectsAabb(const Vec3 &center, double radius, const Vec3 &bmin, const Vec3 &bmax);

struct Hit
{
    bool hit = false;
    Vec3 normal;
    Vec3 point;
    float penetration = 0;
};

Hit sphereHitSphere(const Vec3 &centerA, double radiusA, const Vec3 &centerB, double radiusB);
Hit sphereHitAabb(const Vec3 &center, double radius, const Vec3 &bmin, const Vec3 &bmax);
Hit sphereHitPlane(const Vec3 &center, double radius, const Vec3 &point, const Vec3 &normal);
Hit sphereHitMesh(const Vec3 &localCenter, double localRadius, const MeshGeometry &geometry);

void resolveSphere(Vec3 &center, const Hit &hit);
