#pragma once

#include "Scene.hpp"

#include <cmath>
#include <memory>

class Sphere : public Hittable
{
public:
    Sphere(const Vec3 &center, double radius, const Material &material)
        : center_(center), radius_(radius), material_(material)
    {
    }

    bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const override
    {
        if (radius_ <= 0)
            return false;

        const Vec3 worldCenter = center();
        const double worldRadius = radius_ * parentFrame().scale;
        Vec3 oc = ray.origin - worldCenter;
        double a = dot(ray.direction, ray.direction);
        double halfB = dot(oc, ray.direction);
        double c = dot(oc, oc) - worldRadius * worldRadius;
        double discriminant = halfB * halfB - a * c;
        if (discriminant < 0 || a == 0)
            return false;

        double sqrtDiscriminant = std::sqrt(discriminant);
        double t = (-halfB - sqrtDiscriminant) / a;
        if (t < tMin || t > tMax)
        {
            t = (-halfB + sqrtDiscriminant) / a;
            if (t < tMin || t > tMax)
                return false;
        }

        hit.t = t;
        hit.point = ray.at(t);
        Vec3 outward = normalize(hit.point - worldCenter);
        hit.normal = dot(ray.direction, outward) < 0 ? outward : -outward;
        hit.material = material_;
        return true;
    }

    Vec3 center() const { return parentFrame().point(center_); }
    double worldRadius() const { return radius_ * parentFrame().scale; }
    const Vec3 &localCenter() const { return center_; }
    Vec3 localPosition() const override { return center_; }
    void setLocalPosition(const Vec3 &position) override
    {
        center_ = position;
        notifyTransformChanged();
    }
    Vec3 worldPosition() const override { return center(); }
    double localScale() const override { return radius_; }
    void setLocalScale(double scale) override { radius_ = scale < 0.01 ? 0.01 : scale; }
    double bodyRadius() const override { return worldRadius(); }
    double radius() const { return radius_; }
    void setCenter(const Vec3 &center)
    {
        center_ = center;
        notifyTransformChanged();
    }
    void setRadius(double radius) { radius_ = radius; }

    const char *kind() const override { return "Sphere"; }
    Material material() const override { return material_; }
    void setMaterial(const Material &material) override { material_ = material; }

    Hit contactSphere(const Vec3 &center, double radius) const override;
    bool overlapsSphere(const Vec3 &center, double radius) const override;
    bool blocksPlayer(int playerId) const override;
    ColliderSketch colliderSketch() const override;
    void writeScene(std::ostream &out) const override;
    void contributeGpu(gpu_detail::GpuContribute &sink) const override;
    bool copyShapeFrom(const Hittable &source) override;
    void mixShapeHash(std::uint64_t &hash) const override;

    std::unique_ptr<Hittable> clone() const override
    {
        auto copy = std::make_unique<Sphere>(center_, radius_, material_);
        copyBaseTo(*copy);
        return copy;
    }

private:
    Vec3 center_;
    double radius_;
    Material material_;
};
