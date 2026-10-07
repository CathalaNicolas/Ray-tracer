#pragma once

#include "Object.hpp"

using Mesh = Object;

namespace mesh_detail
{
bool intersect(const Object &object, const Ray &ray, double tMin, double tMax, HitRecord &hit);
Hit contactSphere(const Object &object, const Vec3 &center, double radius);
ColliderSketch colliderSketch(const Object &object);
void writeScene(const Object &object, std::ostream &out);
void mixShapeHash(const Object &object, std::uint64_t &hash);
}
