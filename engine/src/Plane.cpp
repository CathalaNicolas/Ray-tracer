#include "Plane.hpp"

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

Hit Plane::contactSphere(const Vec3 &center, double radius) const
{
    return sphereHitPlane(center, radius, point(), worldNormal());
}

bool Plane::blocksPlayer(int playerId) const
{
    if (rolePassThrough(playerId))
        return false;
    return true;
}

ColliderSketch Plane::colliderSketch() const
{
    ColliderSketch sketch;
    sketch.kind = ColliderSketch::Kind::PlanePatch;
    sketch.center = point();
    sketch.normal = worldNormal();
    return sketch;
}

void Plane::writeScene(std::ostream &out) const
{
    out << "plane \"" << scene_write::escapeName(name) << "\" "
        << point_.x << ' ' << point_.y << ' ' << point_.z << ' '
        << normal_.x << ' ' << normal_.y << ' ' << normal_.z << ' ';
    scene_write::writeMaterial(out, material_);
    if (checker_)
    {
        out << " checker " << checkerAlbedo_.x << ' ' << checkerAlbedo_.y << ' '
            << checkerAlbedo_.z << ' ' << checkerScale_;
    }
    scene_write::writeTag(out, tag);
    scene_write::writeMotion(out, *this);
    scene_write::writeAction(out, *this);
    scene_write::writeParent(out, *this);
    out << '\n';
}

void Plane::contributeGpu(gpu_detail::GpuContribute &sink) const
{
    // Upload keeps the stored normal; parenting for planes is translation-only today.
    sink.plane(*this, point(), normal_, checker_, checkerAlbedo_, checkerScale_);
}

bool Plane::copyShapeFrom(const Hittable &source)
{
    const auto *from = dynamic_cast<const Plane *>(&source);
    if (from == nullptr)
        return false;
    normal_ = from->normal_;
    return true;
}

void Plane::mixShapeHash(std::uint64_t &hash) const
{
    mixBits(hash, 2);
    mixVec(hash, point());
    mixVec(hash, normal_);
    mixBits(hash, checker_ ? 1 : 0);
    mixVec(hash, checkerAlbedo_);
    mixDouble(hash, checkerScale_);
}
