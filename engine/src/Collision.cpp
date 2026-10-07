#include "Collision.hpp"

#include "Mesh.hpp"

#include <algorithm>
#include <cmath>

namespace
{

Vec3 clampToAabb(const Vec3 &center, const Vec3 &bmin, const Vec3 &bmax, Vec3 &boxMin, Vec3 &boxMax)
{
    boxMin = Vec3(std::min(bmin.x, bmax.x), std::min(bmin.y, bmax.y), std::min(bmin.z, bmax.z));
    boxMax = Vec3(std::max(bmin.x, bmax.x), std::max(bmin.y, bmax.y), std::max(bmin.z, bmax.z));
    return Vec3(
        std::clamp(center.x, boxMin.x, boxMax.x),
        std::clamp(center.y, boxMin.y, boxMax.y),
        std::clamp(center.z, boxMin.z, boxMax.z));
}

}

bool sphereIntersectsSphere(const Vec3 &centerA, double radiusA, const Vec3 &centerB, double radiusB)
{
    if (radiusA < 0 || radiusB < 0)
        return false;
    const Vec3 delta = centerA - centerB;
    const double reach = radiusA + radiusB;
    return dot(delta, delta) <= reach * reach;
}

bool sphereIntersectsAabb(const Vec3 &center, double radius, const Vec3 &bmin, const Vec3 &bmax)
{
    if (radius < 0)
        return false;
    Vec3 boxMin;
    Vec3 boxMax;
    const Vec3 closest = clampToAabb(center, bmin, bmax, boxMin, boxMax);
    const Vec3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

Hit sphereHitSphere(const Vec3 &centerA, double radiusA, const Vec3 &centerB, double radiusB)
{
    Hit hit;
    if (radiusA < 0 || radiusB < 0)
        return hit;

    const Vec3 delta = centerA - centerB;
    const double distSq = dot(delta, delta);
    const double reach = radiusA + radiusB;
    if (distSq >= reach * reach)
        return hit;

    const double dist = std::sqrt(distSq);
    hit.hit = true;
    if (dist > 1e-8)
    {
        hit.normal = delta / dist;
        hit.point = centerB + hit.normal * radiusB;
        hit.penetration = static_cast<float>(reach - dist);
    }
    else
    {
        hit.normal = Vec3(0, 1, 0);
        hit.point = centerB + hit.normal * radiusB;
        hit.penetration = static_cast<float>(reach);
    }
    return hit;
}

Hit sphereHitAabb(const Vec3 &center, double radius, const Vec3 &bmin, const Vec3 &bmax)
{
    Hit hit;
    if (radius < 0)
        return hit;

    Vec3 boxMin;
    Vec3 boxMax;
    const Vec3 closest = clampToAabb(center, bmin, bmax, boxMin, boxMax);
    const Vec3 delta = center - closest;
    const double distSq = dot(delta, delta);
    if (distSq > 1e-16)
    {
        if (distSq > radius * radius)
            return hit;
        const double dist = std::sqrt(distSq);
        hit.hit = true;
        hit.normal = delta / dist;
        hit.point = closest;
        hit.penetration = static_cast<float>(radius - dist);
        return hit;
    }

    const double faces[6] = {
        center.x - boxMin.x,
        boxMax.x - center.x,
        center.y - boxMin.y,
        boxMax.y - center.y,
        center.z - boxMin.z,
        boxMax.z - center.z};
    const Vec3 normals[6] = {
        Vec3(-1, 0, 0),
        Vec3(1, 0, 0),
        Vec3(0, -1, 0),
        Vec3(0, 1, 0),
        Vec3(0, 0, -1),
        Vec3(0, 0, 1)};

    int best = 0;
    for (int face = 1; face < 6; ++face)
    {
        if (faces[face] < faces[best])
            best = face;
    }

    hit.hit = true;
    hit.normal = normals[best];
    hit.point = center - normals[best] * faces[best];
    hit.penetration = static_cast<float>(radius + faces[best]);
    return hit;
}

Hit sphereHitPlane(const Vec3 &center, double radius, const Vec3 &point, const Vec3 &normal)
{
    Hit hit;
    if (radius < 0)
        return hit;

    const Vec3 n = normalize(normal);
    if (dot(n, n) == 0)
        return hit;

    const double distance = dot(center - point, n);
    if (distance >= radius)
        return hit;

    hit.hit = true;
    hit.normal = n;
    hit.point = center - n * distance;
    hit.penetration = static_cast<float>(radius - distance);
    return hit;
}

namespace
{

Vec3 closestPointOnTriangle(const Vec3 &point, const Vec3 &a, const Vec3 &b, const Vec3 &c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return a;

    const Vec3 bp = point - b;
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return b;

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        const double v = d1 / (d1 - d3);
        return a + ab * v;
    }

    const Vec3 cp = point - c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        const double w = d2 / (d2 - d6);
        return a + ac * w;
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
    {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }

    const double denom = va + vb + vc;
    if (std::abs(denom) <= 1e-12)
        return a;
    const double v = vb / denom;
    const double w = vc / denom;
    return a + ab * v + ac * w;
}

Hit considerTriangle(const Vec3 &center, double radius, const MeshTri &triangle, const Hit &best)
{
    const Vec3 &a = triangle.position[0];
    const Vec3 &b = triangle.position[1];
    const Vec3 &c = triangle.position[2];
    const Vec3 faceRaw = cross(b - a, c - a);
    const double faceLen = length(faceRaw);
    if (faceLen <= 1e-8)
        return best;
    const Vec3 face = faceRaw / faceLen;
    const Vec3 closest = closestPointOnTriangle(center, a, b, c);
    const Vec3 delta = center - closest;
    const double dist = length(delta);
    const bool onFace = length(closest - (center - face * dot(center - a, face))) <= 1e-4;
    const bool behind = dot(center - a, face) < 0.0;

    Hit hit;
    if (onFace && behind && dist <= radius)
    {
        hit.hit = true;
        hit.normal = face;
        hit.point = closest;
        hit.penetration = static_cast<float>(radius + dist);
    }
    else if (!behind && dist <= radius)
    {
        hit.hit = true;
        hit.point = closest;
        hit.penetration = static_cast<float>(radius - dist);
        if (dist > 1e-8)
            hit.normal = delta / dist;
        else
            hit.normal = face;
        if (dot(hit.normal, face) < 0.0)
            hit.normal = face;
    }
    else
    {
        return best;
    }

    if (best.hit && best.penetration >= hit.penetration)
        return best;
    return hit;
}

void queryMesh(const Vec3 &center, double radius, const MeshGeometry &geometry, int nodeIndex, Hit &best)
{
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= geometry.nodes.size())
        return;
    const BvhNode &node = geometry.nodes[static_cast<size_t>(nodeIndex)];
    if (!sphereIntersectsAabb(center, radius, node.boundsMin, node.boundsMax))
        return;
    if (node.right < 0)
    {
        const int begin = node.left;
        const int count = -node.right;
        for (int index = begin; index < begin + count; ++index)
        {
            if (index < 0 || static_cast<size_t>(index) >= geometry.triangles.size())
                continue;
            best = considerTriangle(center, radius, geometry.triangles[static_cast<size_t>(index)], best);
        }
        return;
    }
    queryMesh(center, radius, geometry, node.left, best);
    queryMesh(center, radius, geometry, node.right, best);
}

}

Hit sphereHitMesh(const Vec3 &localCenter, double localRadius, const MeshGeometry &geometry)
{
    Hit best;
    if (localRadius < 0 || geometry.root < 0)
        return best;
    queryMesh(localCenter, localRadius, geometry, geometry.root, best);
    return best;
}

void resolveSphere(Vec3 &center, const Hit &hit)
{
    if (!hit.hit || hit.penetration <= 0.f)
        return;

    Vec3 normal = hit.normal;
    const double len = length(normal);
    if (len <= 1e-8)
        normal = Vec3(0, 1, 0);
    else
        normal = normal / len;
    center += normal * static_cast<double>(hit.penetration);
}
