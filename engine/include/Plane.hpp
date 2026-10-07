#pragma once

#include "Scene.hpp"

#include <cmath>
#include <memory>

class Plane : public Hittable
{
public:
    Plane(const Vec3 &point, const Vec3 &normal, const Material &material)
        : point_(point), normal_(normalize(normal)), material_(material)
    {
        if (length(normal) == 0)
            normal_ = Vec3(0, 1, 0);
    }

    Plane &setChecker(const Vec3 &albedo, double scale)
    {
        checker_ = true;
        checkerAlbedo_ = albedo;
        checkerScale_ = scale > 0 ? scale : 1;
        return *this;
    }

    Vec3 point() const { return parentFrame().point(point_); }
    const Vec3 &localPoint() const { return point_; }
    Vec3 localPosition() const override { return point_; }
    void setLocalPosition(const Vec3 &position) override
    {
        point_ = position;
        notifyTransformChanged();
    }
    Vec3 worldPosition() const override { return point(); }
    const Vec3 &normal() const { return normal_; }
    Vec3 worldNormal() const
    {
        const Vec3 turned = parentFrame().direction(normal_);
        return length(turned) == 0 ? Vec3(0, 1, 0) : normalize(turned);
    }
    void setPoint(const Vec3 &point)
    {
        point_ = point;
        notifyTransformChanged();
    }

    void setNormal(const Vec3 &normal)
    {
        normal_ = length(normal) == 0 ? Vec3(0, 1, 0) : normalize(normal);
        notifyTransformChanged();
    }

    bool checker() const { return checker_; }
    void setCheckerEnabled(bool enabled) { checker_ = enabled; }
    const Vec3 &checkerAlbedo() const { return checkerAlbedo_; }
    void setCheckerAlbedo(const Vec3 &albedo) { checkerAlbedo_ = albedo; }
    double checkerScale() const { return checkerScale_; }
    void setCheckerScale(double scale) { checkerScale_ = scale > 0 ? scale : 1; }

    const char *kind() const override { return "Plane"; }
    Material material() const override { return material_; }
    void setMaterial(const Material &material) override { material_ = material; }

    Hit contactSphere(const Vec3 &center, double radius) const override;
    bool blocksPlayer(int playerId) const override;
    ColliderSketch colliderSketch() const override;
    void writeScene(std::ostream &out) const override;
    void contributeGpu(gpu_detail::GpuContribute &sink) const override;
    bool copyShapeFrom(const Hittable &source) override;
    void mixShapeHash(std::uint64_t &hash) const override;

    std::unique_ptr<Hittable> clone() const override
    {
        auto copy = std::make_unique<Plane>(point_, normal_, material_);
        copyBaseTo(*copy);
        copy->checker_ = checker_;
        copy->checkerAlbedo_ = checkerAlbedo_;
        copy->checkerScale_ = checkerScale_;
        return copy;
    }

    bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const override
    {
        const Vec3 worldNormal = this->worldNormal();
        double denom = dot(ray.direction, worldNormal);
        if (std::abs(denom) < 1e-8)
            return false;

        const Vec3 worldPoint = point();
        double t = dot(worldPoint - ray.origin, worldNormal) / denom;
        if (t < tMin || t > tMax)
            return false;

        hit.t = t;
        hit.point = ray.at(t);
        hit.normal = denom < 0 ? worldNormal : -worldNormal;
        hit.material = material_;
        if (checker_)
        {
            int ix = static_cast<int>(std::floor(hit.point.x / checkerScale_));
            int iz = static_cast<int>(std::floor(hit.point.z / checkerScale_));
            if (((ix + iz) & 1) != 0)
                hit.material.albedo = checkerAlbedo_;
        }
        return true;
    }

private:
    Vec3 point_;
    Vec3 normal_;
    Material material_;
    bool checker_ = false;
    Vec3 checkerAlbedo_{0.2, 0.2, 0.22};
    double checkerScale_ = 1;
};
