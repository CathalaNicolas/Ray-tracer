#pragma once

#include "Components.hpp"
#include "Hit.hpp"
#include "Ray.hpp"

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

class Scene;

class Object
{
public:
    Object() = default;
    Object(Scene *scene, EntityId id) : scene_(scene), id_(id) {}

    EntityId id() const { return id_; }
    EntityId parentId() const;
    void setParentId(EntityId parent);

    std::string &name();
    const std::string &name() const;
    std::string &tag();
    const std::string &tag() const;
    void setTag(const std::string &value);
    void ensureRole() const;
    std::shared_ptr<Role> role() const;
    int &layer();
    int layer() const;
    std::string &prefab();
    const std::string &prefab() const;
    std::string &instanceOf();
    const std::string &instanceOf() const;
    Motion &motion();
    const Motion &motion() const;
    Action &action();
    const Action &action() const;
    double &spawnEvery();
    double spawnEvery() const;
    int &spawnClock();
    int spawnClock() const;
    EntityId &spawnedId();
    EntityId spawnedId() const;

    const char *kind() const;
    Material material() const;
    void setMaterial(const Material &material);
    bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const;

    Vec3 localPosition() const;
    Vec3 localPositionValue() const { return localPosition(); }
    Vec3 worldPosition() const;
    void setLocalPosition(const Vec3 &position);
    void setWorldPosition(const Vec3 &position);
    glm::dquat localRotationQuat() const;
    Vec3 localRotation() const;
    void setLocalRotation(const Vec3 &degrees);
    void setLocalRotationQuat(const glm::dquat &rotation);
    glm::dvec3 localScaleVec() const;
    double localScale() const;
    void setLocalScale(double scale);
    void setLocalScaleVec(const glm::dvec3 &scale);
    glm::dmat4 worldMatrix() const;
    glm::dmat4 displayWorldMatrix() const;
    Vec3 displayWorldPosition() const;
    double displayWorldScale() const;
    double displayWorldRadius() const;
    Vec3 displayWorldNormal() const;
    void displayWorldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const;
    void setDisplayPose(const glm::dvec3 &position, const glm::dquat &rotation, const glm::dvec3 &scale);
    void clearDisplayPose();
    double bodyRadius() const;

    Hit contactSphere(const Vec3 &center, double radius) const;
    bool overlapsSphere(const Vec3 &center, double radius) const;
    bool blocksPlayer(EntityId playerId) const;
    bool blocksCamera(EntityId playerId) const;
    ColliderSketch colliderSketch() const;
    void writeScene(std::ostream &out) const;
    bool copyShapeFrom(const Object &source);
    void mixShapeHash(std::uint64_t &hash) const;
    EntityId clone(Scene &destination) const;

    bool isSphere() const;
    bool isPlane() const;
    bool isMesh() const;
    bool isTerrain() const;
    const std::shared_ptr<TerrainTile> &terrainTile() const;
    bool loadTerrain(const std::filesystem::path &path, std::string &error);
    void setTerrainData(std::shared_ptr<TerrainTile> tile, std::shared_ptr<MeshGeometry> mesh);

    Vec3 center() const { return worldPosition(); }
    Vec3 position() const { return worldPosition(); }
    Vec3 localCenter() const { return localPosition(); }
    double radius() const;
    double worldRadius() const;
    void setCenter(const Vec3 &center) { setLocalPosition(center); }
    void setRadius(double radius);

    Vec3 point() const { return worldPosition(); }
    Vec3 localPoint() const { return localPosition(); }
    Vec3 normal() const;
    Vec3 worldNormal() const;
    void setPoint(const Vec3 &point) { setLocalPosition(point); }
    void setNormal(const Vec3 &normal);
    bool checker() const;
    Object &setChecker(const Vec3 &albedo, double scale);
    void setCheckerEnabled(bool enabled);
    Vec3 checkerAlbedo() const;
    void setCheckerAlbedo(const Vec3 &albedo);
    double checkerScale() const;
    void setCheckerScale(double scale);

    const std::shared_ptr<MeshGeometry> &geometry() const;
    const std::vector<MeshTri> &triangles() const;
    const std::string &sourcePath() const;
    void setTriangles(std::vector<MeshTri> triangles);
    bool loadMesh(const std::filesystem::path &path, std::string &error);
    bool load(const std::filesystem::path &path, std::string &error) { return loadMesh(path, error); }
    bool refreshFromDisk(std::string &error);
    void worldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const;
    double worldScale() const;
    Vec3 worldScaleVec() const;
    void setPosition(const Vec3 &position) { setLocalPosition(position); }
    void setRotation(const Vec3 &degrees) { setLocalRotation(degrees); }
    Vec3 rotation() const { return localRotation(); }
    void setScale(double scale) { setLocalScale(scale); }
    double scale() const { return localScale(); }

    Scene *scene() const { return scene_; }
    void notifyTransformChanged() const;
    void bindScene(Scene *) {}

private:
    Scene *scene_ = nullptr;
    EntityId id_ = kInvalidEntityId;
};
