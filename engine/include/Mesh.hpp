#pragma once

#include "Constants.hpp"
#include "Scene.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

inline bool meshRotationActive(const Vec3 &degrees)
{
    return std::abs(degrees.x) > 1e-8 || std::abs(degrees.y) > 1e-8 || std::abs(degrees.z) > 1e-8;
}

inline Vec3 meshRotate(const Vec3 &value, const Vec3 &degrees)
{
    if (!meshRotationActive(degrees))
        return value;
    const double yaw = degrees.y * kPi / 180.0;
    const double pitch = degrees.x * kPi / 180.0;
    const double roll = degrees.z * kPi / 180.0;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const Vec3 yawed(cy * value.x + sy * value.z, value.y, -sy * value.x + cy * value.z);
    const Vec3 pitched(yawed.x, cp * yawed.y - sp * yawed.z, sp * yawed.y + cp * yawed.z);
    return Vec3(cr * pitched.x - sr * pitched.y, sr * pitched.x + cr * pitched.y, pitched.z);
}

inline Vec3 meshRotateInverse(const Vec3 &value, const Vec3 &degrees)
{
    if (!meshRotationActive(degrees))
        return value;
    const double yaw = degrees.y * kPi / 180.0;
    const double pitch = degrees.x * kPi / 180.0;
    const double roll = degrees.z * kPi / 180.0;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const Vec3 rolled(cr * value.x + sr * value.y, -sr * value.x + cr * value.y, value.z);
    const Vec3 pitched(rolled.x, cp * rolled.y + sp * rolled.z, -sp * rolled.y + cp * rolled.z);
    return Vec3(cy * pitched.x - sy * pitched.z, pitched.y, sy * pitched.x + cy * pitched.z);
}

struct MeshTri
{
    Vec3 position[3];
    Vec3 normal[3];
    float u[3]{};
    float v[3]{};
};

struct BvhNode
{
    Vec3 boundsMin;
    Vec3 boundsMax;
    int left = 0;
    int right = 0;
};

int buildBvh(std::vector<MeshTri> &triangles, std::vector<BvhNode> &nodes);

struct MeshGeometry
{
    std::vector<MeshTri> triangles;
    std::vector<BvhNode> nodes;
    int root = -1;
    Vec3 boundsMin;
    Vec3 boundsMax;
    std::string sourcePath;
};

class Mesh : public Hittable
{
public:
    bool load(const std::filesystem::path &path, std::string &error);
    bool refreshFromDisk(std::string &error);
    void setTriangles(std::vector<MeshTri> triangles);

    Vec3 position() const { return parentFrame().point(position_); }
    const Vec3 &localPositionValue() const { return position_; }
    Vec3 localPosition() const override { return position_; }
    void setLocalPosition(const Vec3 &position) override
    {
        position_ = position;
        notifyTransformChanged();
    }
    Vec3 worldPosition() const override { return position(); }
    void setPosition(const Vec3 &position)
    {
        position_ = position;
        notifyTransformChanged();
    }
    double scale() const { return scale_; }
    double localScale() const override { return scale_; }
    void setLocalScale(double scale) override { setScale(scale); }
    Vec3 localRotation() const override { return rotation_; }
    void setLocalRotation(const Vec3 &degrees) override
    {
        rotation_ = degrees;
        notifyTransformChanged();
    }
    double worldScale() const { return scale_ * parentFrame().scale; }
    void worldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const;
    void setScale(double scale)
    {
        scale_ = scale > 0 ? scale : 0.01;
        notifyTransformChanged();
    }
    const Vec3 &rotation() const { return rotation_; }
    void setRotation(const Vec3 &degrees)
    {
        rotation_ = degrees;
        notifyTransformChanged();
    }
    const std::string &sourcePath() const { return sourcePath_; }
    const std::shared_ptr<MeshGeometry> &geometry() const { return geometry_; }
    const std::vector<MeshTri> &triangles() const;

    const char *kind() const override { return "Mesh"; }
    Material material() const override { return material_; }
    void setMaterial(const Material &material) override { material_ = material; }
    bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const override;
    Hit contactSphere(const Vec3 &center, double radius) const override;
    bool blocksPlayer(int playerId) const override;
    ColliderSketch colliderSketch() const override;
    void writeScene(std::ostream &out) const override;
    void contributeGpu(gpu_detail::GpuContribute &sink) const override;
    bool copyShapeFrom(const Hittable &source) override;
    void mixShapeHash(std::uint64_t &hash) const override;
    void applyParentAxes(ParentFrame &frame) const override;
    std::unique_ptr<Hittable> clone() const override;

private:
    std::shared_ptr<MeshGeometry> geometry_;
    Vec3 position_;
    Vec3 rotation_;
    double scale_ = 1;
    Material material_ = Material::makeDiffuse(Vec3(0.7, 0.7, 0.72));
    std::string sourcePath_;
    std::int64_t sourceStamp_ = 0;
    std::uintmax_t sourceBytes_ = 0;
};
