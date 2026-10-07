#include "Sphere.hpp"

#include "GpuContribute.hpp"
#include "SceneWrite.hpp"

#include <cstring>

namespace
{

void mixBits(std::uint64_t &hash, std::uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void mixDouble(std::uint64_t &hash, double value)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "fingerprint packs each double as 64 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    mixBits(hash, bits);
}

void mixVec(std::uint64_t &hash, const Vec3 &value)
{
    mixDouble(hash, value.x);
    mixDouble(hash, value.y);
    mixDouble(hash, value.z);
}

}

Hit Sphere::contactSphere(const Vec3 &center, double radius) const
{
    return sphereHitSphere(center, radius, this->center(), worldRadius());
}

bool Sphere::overlapsSphere(const Vec3 &center, double radius) const
{
    return sphereIntersectsSphere(center, radius, this->center(), worldRadius());
}

bool Sphere::blocksPlayer(int playerId) const
{
    if (rolePassThrough(playerId))
        return false;
    return true;
}

ColliderSketch Sphere::colliderSketch() const
{
    ColliderSketch sketch;
    sketch.kind = ColliderSketch::Kind::Sphere;
    sketch.center = center();
    sketch.radius = worldRadius();
    sketch.player = false;
    ensureRole();
    if (role && role->kind() == RoleKind::Player)
        sketch.player = true;
    return sketch;
}

void Sphere::writeScene(std::ostream &out) const
{
    out << "sphere \"" << scene_write::escapeName(name) << "\" "
        << center_.x << ' ' << center_.y << ' ' << center_.z << ' '
        << radius_ << ' ';
    scene_write::writeMaterial(out, material_);
    scene_write::writeTag(out, tag);
    scene_write::writeMotion(out, *this);
    scene_write::writeAction(out, *this);
    scene_write::writeParent(out, *this);
    out << '\n';
}

void Sphere::contributeGpu(gpu_detail::GpuContribute &sink) const
{
    sink.sphere(*this, center(), worldRadius());
}

bool Sphere::copyShapeFrom(const Hittable &source)
{
    const auto *from = dynamic_cast<const Sphere *>(&source);
    if (from == nullptr)
        return false;
    radius_ = from->radius_;
    return true;
}

void Sphere::mixShapeHash(std::uint64_t &hash) const
{
    mixBits(hash, 1);
    mixVec(hash, center());
    mixDouble(hash, worldRadius());
    mixDouble(hash, radius_);
}
