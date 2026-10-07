#include "Mesh.hpp"

#include "Hit.hpp"
#include "Scene.hpp"
#include "SceneWrite.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>

namespace
{

Vec3 min3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

Vec3 max3(const Vec3 &a, const Vec3 &b)
{
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

Vec3 centroid(const MeshTri &triangle)
{
    return (triangle.position[0] + triangle.position[1] + triangle.position[2]) / 3.0;
}

double axisOf(const Vec3 &value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 1)
        return value.y;
    return value.z;
}

int buildRange(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes, int begin, int end)
{
    const int self = static_cast<int>(nodes.size());
    nodes.push_back({});

    Vec3 boundsMin = triangles[static_cast<size_t>(begin)].position[0];
    Vec3 boundsMax = boundsMin;
    for (int index = begin; index < end; ++index)
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            boundsMin = min3(boundsMin, triangles[static_cast<size_t>(index)].position[corner]);
            boundsMax = max3(boundsMax, triangles[static_cast<size_t>(index)].position[corner]);
        }
    }
    const Vec3 pad(1e-4, 1e-4, 1e-4);
    nodes[static_cast<size_t>(self)].boundsMin = boundsMin - pad;
    nodes[static_cast<size_t>(self)].boundsMax = boundsMax + pad;

    const int count = end - begin;
    if (count <= 4)
    {
        nodes[static_cast<size_t>(self)].left = begin;
        nodes[static_cast<size_t>(self)].right = -count;
        return self;
    }

    const Vec3 extent = boundsMax - boundsMin;
    int axis = 0;
    if (extent.y >= extent.x && extent.y >= extent.z)
        axis = 1;
    else if (extent.z >= extent.x && extent.z >= extent.y)
        axis = 2;
    const int mid = begin + count / 2;
    std::nth_element(
        triangles.begin() + begin,
        triangles.begin() + mid,
        triangles.begin() + end,
        [axis](const MeshTri &a, const MeshTri &b) {
            return axisOf(centroid(a), axis) < axisOf(centroid(b), axis);
        });

    const int left = buildRange(triangles, nodes, begin, mid);
    const int right = buildRange(triangles, nodes, mid, end);
    nodes[static_cast<size_t>(self)].left = left;
    nodes[static_cast<size_t>(self)].right = right;
    return self;
}

bool slab(const Vec3 &origin, const Vec3 &direction, const BvhNode &node, double tMin, double tMax)
{
    const double originAxis[3] = {origin.x, origin.y, origin.z};
    const double directionAxis[3] = {direction.x, direction.y, direction.z};
    const double minAxis[3] = {node.boundsMin.x, node.boundsMin.y, node.boundsMin.z};
    const double maxAxis[3] = {node.boundsMax.x, node.boundsMax.y, node.boundsMax.z};
    double enter = tMin;
    double exit = tMax;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(directionAxis[axis]) < 1e-12)
        {
            if (originAxis[axis] < minAxis[axis] || originAxis[axis] > maxAxis[axis])
                return false;
            continue;
        }
        const double inverse = 1.0 / directionAxis[axis];
        double t1 = (minAxis[axis] - originAxis[axis]) * inverse;
        double t2 = (maxAxis[axis] - originAxis[axis]) * inverse;
        if (t1 > t2)
            std::swap(t1, t2);
        enter = std::max(enter, t1);
        exit = std::min(exit, t2);
        if (enter > exit)
            return false;
    }
    return true;
}

bool hitTriangle(const MeshTri &triangle, const Ray &ray, const Vec3 &origin, const Vec3 &direction, double tMin, double tMax, HitRecord &hit)
{
    const Vec3 edge1 = triangle.position[1] - triangle.position[0];
    const Vec3 edge2 = triangle.position[2] - triangle.position[0];
    const Vec3 p = cross(direction, edge2);
    const double determinant = dot(edge1, p);
    if (std::abs(determinant) < 1e-8)
        return false;
    const double inverse = 1.0 / determinant;
    const Vec3 s = origin - triangle.position[0];
    const double u = dot(s, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    const Vec3 q = cross(s, edge1);
    const double v = dot(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return false;
    const double t = dot(edge2, q) * inverse;
    if (t < tMin || t > tMax)
        return false;

    const double w = 1.0 - u - v;
    Vec3 normal = triangle.normal[0] * w + triangle.normal[1] * u + triangle.normal[2] * v;
    if (length(normal) <= 1e-8)
        normal = cross(edge1, edge2);
    normal = normalize(normal);
    if (dot(direction, normal) > 0)
        normal = -normal;

    hit.t = t;
    hit.point = ray.at(t);
    hit.normal = normal;
    return true;
}

bool sphereIntersectsAabb(const Vec3 &center, double radius, const Vec3 &bmin, const Vec3 &bmax)
{
    if (radius < 0)
        return false;
    const Vec3 boxMin(std::min(bmin.x, bmax.x), std::min(bmin.y, bmax.y), std::min(bmin.z, bmax.z));
    const Vec3 boxMax(std::max(bmin.x, bmax.x), std::max(bmin.y, bmax.y), std::max(bmin.z, bmax.z));
    const Vec3 closest(
        std::clamp(center.x, boxMin.x, boxMax.x),
        std::clamp(center.y, boxMin.y, boxMax.y),
        std::clamp(center.z, boxMin.z, boxMax.z));
    const Vec3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

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
        return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = point - c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double denom = va + vb + vc;
    if (std::abs(denom) <= 1e-12)
        return a;
    return a + ab * (vb / denom) + ac * (vc / denom);
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
        hit.normal = dist > 1e-8 ? delta / dist : face;
        if (dot(hit.normal, face) < 0.0)
            hit.normal = face;
    }
    else
        return best;
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

Hit sphereHitMesh(const Vec3 &localCenter, double localRadius, const MeshGeometry &geometry)
{
    Hit best;
    if (localRadius < 0 || geometry.root < 0)
        return best;
    queryMesh(localCenter, localRadius, geometry, geometry.root, best);
    return best;
}

} // namespace

int buildBvh(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes)
{
    nodes.clear();
    if (triangles.empty())
        return -1;
    return buildRange(triangles, nodes, 0, static_cast<int>(triangles.size()));
}

namespace
{

void indexedToTriangles(MeshGeometry &geometry)
{
    geometry.triangles.clear();
    if (geometry.positions.empty() || geometry.indices.size() < 3)
        return;
    const size_t triCount = geometry.indices.size() / 3;
    geometry.triangles.resize(triCount);
    for (size_t t = 0; t < triCount; ++t)
    {
        MeshTri &triangle = geometry.triangles[t];
        for (int corner = 0; corner < 3; ++corner)
        {
            const std::uint32_t index = geometry.indices[t * 3 + static_cast<size_t>(corner)];
            if (index >= geometry.positions.size())
            {
                geometry.triangles.clear();
                return;
            }
            triangle.position[corner] = geometry.positions[index];
            triangle.normal[corner] =
                index < geometry.normals.size() ? geometry.normals[index] : Vec3(0, 1, 0);
            triangle.u[corner] = index < geometry.u.size() ? geometry.u[index] : 0.f;
            triangle.v[corner] = index < geometry.v.size() ? geometry.v[index] : 0.f;
        }
    }
}

void trianglesToIndexed(MeshGeometry &geometry)
{
    geometry.positions.clear();
    geometry.normals.clear();
    geometry.u.clear();
    geometry.v.clear();
    geometry.indices.clear();
    if (geometry.triangles.empty())
        return;

    const size_t cornerCount = geometry.triangles.size() * 3;
    std::vector<float> packed(cornerCount * 8);
    for (size_t t = 0; t < geometry.triangles.size(); ++t)
    {
        const MeshTri &triangle = geometry.triangles[t];
        for (int corner = 0; corner < 3; ++corner)
        {
            const size_t at = t * 3 + static_cast<size_t>(corner);
            packed[at * 8 + 0] = static_cast<float>(triangle.position[corner].x);
            packed[at * 8 + 1] = static_cast<float>(triangle.position[corner].y);
            packed[at * 8 + 2] = static_cast<float>(triangle.position[corner].z);
            packed[at * 8 + 3] = static_cast<float>(triangle.normal[corner].x);
            packed[at * 8 + 4] = static_cast<float>(triangle.normal[corner].y);
            packed[at * 8 + 5] = static_cast<float>(triangle.normal[corner].z);
            packed[at * 8 + 6] = triangle.u[corner];
            packed[at * 8 + 7] = triangle.v[corner];
        }
    }

    std::vector<unsigned int> remap(cornerCount);
    const size_t unique = meshopt_generateVertexRemap(remap.data(), nullptr, cornerCount, packed.data(), cornerCount,
        sizeof(float) * 8);
    geometry.positions.resize(unique);
    geometry.normals.resize(unique);
    geometry.u.resize(unique);
    geometry.v.resize(unique);
    geometry.indices.resize(cornerCount);
    for (size_t i = 0; i < cornerCount; ++i)
    {
        const unsigned int mapped = remap[i];
        geometry.positions[mapped] = Vec3(packed[i * 8 + 0], packed[i * 8 + 1], packed[i * 8 + 2]);
        geometry.normals[mapped] = Vec3(packed[i * 8 + 3], packed[i * 8 + 4], packed[i * 8 + 5]);
        geometry.u[mapped] = packed[i * 8 + 6];
        geometry.v[mapped] = packed[i * 8 + 7];
        geometry.indices[i] = mapped;
    }
}

} // namespace

MeshLod meshLodFromTriangles(const std::vector<MeshTri> &triangles)
{
    MeshGeometry temporary;
    temporary.triangles = triangles;
    trianglesToIndexed(temporary);
    MeshLod lod;
    lod.positions = std::move(temporary.positions);
    lod.normals = std::move(temporary.normals);
    lod.u = std::move(temporary.u);
    lod.v = std::move(temporary.v);
    lod.indices = std::move(temporary.indices);
    return lod;
}

void meshAssignTriangles(MeshGeometry &geometry, std::vector<MeshTri> triangles)
{
    geometry.positions.clear();
    geometry.normals.clear();
    geometry.u.clear();
    geometry.v.clear();
    geometry.indices.clear();
    geometry.extraLods.clear();
    geometry.triangles = std::move(triangles);
    finishGeometry(geometry);
}

void finishGeometry(MeshGeometry &geometry)
{
    if (!geometry.positions.empty() && geometry.indices.size() >= 3)
        indexedToTriangles(geometry);
    else if (!geometry.triangles.empty())
        trianglesToIndexed(geometry);

    geometry.root = buildBvh(geometry.triangles, geometry.nodes);
    if (geometry.root < 0)
    {
        geometry.contentHash = 0;
        return;
    }
    geometry.boundsMin = geometry.nodes[static_cast<size_t>(geometry.root)].boundsMin;
    geometry.boundsMax = geometry.nodes[static_cast<size_t>(geometry.root)].boundsMax;
    std::uint64_t hash = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint64_t value) {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    auto mixDouble = [&](double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mix(bits);
    };
    mix(geometry.positions.size());
    mix(geometry.indices.size());
    for (const Vec3 &position : geometry.positions)
    {
        mixDouble(position.x);
        mixDouble(position.y);
        mixDouble(position.z);
    }
    for (std::uint32_t index : geometry.indices)
        mix(index);
    mix(std::hash<std::string>{}(geometry.sourcePath));
    geometry.contentHash = hash == 0 ? 1 : hash;
}

namespace mesh_detail
{

bool intersect(const Object &object, const Ray &ray, double tMin, double tMax, HitRecord &hit)
{
    const auto &geometry = object.geometry();
    const double placedScale = object.worldScale();
    if (!geometry || geometry->root < 0 || geometry->triangles.empty() || placedScale <= 1e-8)
        return false;

    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    object.worldAxes(axisX, axisY, axisZ);
    const Vec3 delta = ray.origin - object.worldPosition();
    Vec3 origin(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
    Vec3 direction(dot(ray.direction, axisX), dot(ray.direction, axisY), dot(ray.direction, axisZ));
    origin = origin / placedScale;
    direction = direction / placedScale;
    int stack[64];
    int top = 0;
    stack[top++] = geometry->root;
    bool found = false;
    double closest = tMax;

    while (top > 0)
    {
        const BvhNode &node = geometry->nodes[static_cast<size_t>(stack[--top])];
        if (!slab(origin, direction, node, tMin, closest))
            continue;
        if (node.right < 0)
        {
            const int count = -node.right;
            for (int index = 0; index < count; ++index)
            {
                HitRecord candidate;
                if (hitTriangle(geometry->triangles[static_cast<size_t>(node.left + index)], ray, origin, direction, tMin, closest, candidate))
                {
                    found = true;
                    closest = candidate.t;
                    hit = candidate;
                }
            }
        }
        else if (top < 62)
        {
            stack[top++] = node.left;
            stack[top++] = node.right;
        }
    }

    if (!found)
        return false;
    hit.normal = axisX * hit.normal.x + axisY * hit.normal.y + axisZ * hit.normal.z;
    if (length(hit.normal) > 1e-8)
        hit.normal = normalize(hit.normal);
    hit.material = object.material();
    return true;
}

Hit contactSphere(const Object &object, const Vec3 &center, double radius)
{
    const auto &geometry = object.geometry();
    const double placedScale = object.worldScale();
    if (!geometry || geometry->root < 0 || placedScale <= 1e-8)
        return {};
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    object.worldAxes(axisX, axisY, axisZ);
    const Vec3 delta = center - object.worldPosition();
    const Vec3 localCenter(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
    Hit hit = sphereHitMesh(localCenter / placedScale, radius / placedScale, *geometry);
    if (!hit.hit)
        return hit;
    hit.normal = axisX * hit.normal.x + axisY * hit.normal.y + axisZ * hit.normal.z;
    if (length(hit.normal) > 1e-8)
        hit.normal = normalize(hit.normal);
    hit.point = object.worldPosition() + (axisX * hit.point.x + axisY * hit.point.y + axisZ * hit.point.z) * placedScale;
    hit.penetration = static_cast<float>(static_cast<double>(hit.penetration) * placedScale);
    return hit;
}

ColliderSketch colliderSketch(const Object &object)
{
    ColliderSketch sketch;
    sketch.center = object.worldPosition();
    const auto &geometry = object.geometry();
    if (!geometry || geometry->root < 0)
    {
        sketch.kind = ColliderSketch::Kind::MeshPoint;
        return sketch;
    }
    sketch.kind = ColliderSketch::Kind::MeshBox;
    object.worldAxes(sketch.axisX, sketch.axisY, sketch.axisZ);
    sketch.localMin = geometry->boundsMin;
    sketch.localMax = geometry->boundsMax;
    sketch.radius = object.worldScale();
    return sketch;
}

void writeScene(const Object &object, std::ostream &out)
{
    const Vec3 position = object.localPosition();
    const Vec3 rotation = object.localRotation();
    out << "mesh \"" << scene_write::escapeName(object.name()) << "\" "
        << position.x << ' ' << position.y << ' ' << position.z << ' '
        << object.localScale() << ' ';
    scene_write::writeMaterial(out, object.material());
    if (meshRotationActive(rotation))
        out << " rot " << rotation.x << ' ' << rotation.y << ' ' << rotation.z;
    std::string extension;
    if (object.sourcePath().size() >= 4)
        extension = object.sourcePath().substr(object.sourcePath().size() - 4);
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    out << (extension == ".fbx" ? " fbx \"" : " obj \"") << scene_write::escapeName(object.sourcePath()) << '"';
    scene_write::writeObjectTail(out, object);
    out << '\n';
}

void mixShapeHash(const Object &object, std::uint64_t &hash)
{
    auto mixBits = [&](std::uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };
    auto mixDouble = [&](double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mixBits(bits);
    };
    auto mixVec = [&](const Vec3 &value) {
        mixDouble(value.x);
        mixDouble(value.y);
        mixDouble(value.z);
    };
    auto mixText = [&](const std::string &text) {
        for (unsigned char character : text)
            mixBits(character);
    };
    mixBits(3);
    mixVec(object.worldPosition());
    mixDouble(object.worldScale());
    mixVec(object.localRotation());
    mixDouble(object.localScale());
    mixText(object.sourcePath());
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
    object.worldAxes(axisX, axisY, axisZ);
    mixVec(axisX);
    mixVec(axisY);
    mixVec(axisZ);
    const MeshGeometry *geometry = object.geometry().get();
    mixBits(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(geometry)));
    if (geometry != nullptr)
    {
        mixBits(geometry->triangles.size());
        mixVec(geometry->boundsMin);
        mixVec(geometry->boundsMax);
    }
}

} // namespace mesh_detail
