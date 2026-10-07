#include "Object.hpp"

#include "EngineSettings.hpp"
#include "Mesh.hpp"
#include "MeshLoad.hpp"
#include "Scene.hpp"
#include "SceneWrite.hpp"
#include "Terrain.hpp"
#include "TransformMath.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <unordered_set>

#include <glm/gtc/matrix_inverse.hpp>

namespace
{
template <typename T>
T &component(Object &object)
{
    return object.scene()->registry().get<T>(object.scene()->entity(object.id()));
}

template <typename T>
const T &component(const Object &object)
{
    return object.scene()->registry().get<T>(object.scene()->entity(object.id()));
}

void mixBits(std::uint64_t &hash, std::uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void mixDouble(std::uint64_t &hash, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    mixBits(hash, bits);
}

void mixVec(std::uint64_t &hash, const Vec3 &value)
{
    mixDouble(hash, value.x);
    mixDouble(hash, value.y);
    mixDouble(hash, value.z);
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

double matrixMaxAxisScale(const glm::dmat4 &matrix)
{
    const double x = glm::length(glm::dvec3(matrix[0]));
    const double y = glm::length(glm::dvec3(matrix[1]));
    const double z = glm::length(glm::dvec3(matrix[2]));
    return std::max(x, std::max(y, z));
}

Vec3 matrixAxisScales(const glm::dmat4 &matrix)
{
    return Vec3(
        glm::length(glm::dvec3(matrix[0])),
        glm::length(glm::dvec3(matrix[1])),
        glm::length(glm::dvec3(matrix[2])));
}
}

namespace
{

EntityId newIdPrefix()
{
    std::random_device device;
    const std::uint64_t mix = (static_cast<std::uint64_t>(device()) << 32) ^ device()
        ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uint64_t prefix = mix << 32;
    if (prefix == 0 || prefix == kTerrainTileEntityPrefix)
        prefix = 1ull << 32;
    return prefix;
}

} // namespace

Scene::Scene()
    : sessionPrefix_(newIdPrefix()), nextId_(makeEntityId(sessionPrefix_, 1))
{
    if (sessionPrefix_ == 0)
        sessionPrefix_ = 1ull << 32;
    if (entityIdPrefix(nextId_) != sessionPrefix_)
        nextId_ = makeEntityId(sessionPrefix_, 1);
}

void Scene::setNextId(EntityId next)
{
    if (next == kInvalidEntityId)
        return;
    if (entityIdPrefix(next) != sessionPrefix_)
        return;
    const EntityId counter = entityIdCounter(next);
    if (counter > entityIdCounter(nextId_) || entityIdPrefix(nextId_) != sessionPrefix_)
        nextId_ = makeEntityId(sessionPrefix_, counter == 0 ? 1 : counter);
}

void Scene::restoreIdState(EntityId next)
{
    if (next == kInvalidEntityId)
        return;
    sessionPrefix_ = entityIdPrefix(next);
    if (sessionPrefix_ == 0)
        sessionPrefix_ = 1ull << 32;
    EntityId counter = entityIdCounter(next);
    if (counter == 0)
        counter = 1;
    nextId_ = makeEntityId(sessionPrefix_, counter);
}

void Scene::retainSessionIds(EntityId prefix, EntityId next)
{
    sessionPrefix_ = entityIdPrefix(prefix);
    if (sessionPrefix_ == 0)
        sessionPrefix_ = 1ull << 32;
    EntityId counter = entityIdCounter(next);
    if (counter == 0)
        counter = 1;
    nextId_ = makeEntityId(sessionPrefix_, counter);
}

void Scene::markPhysicsDirty(EntityId id)
{
    if (id != kInvalidEntityId)
        physicsDirty_.insert(id);
}

void Scene::clearPhysicsDirty()
{
    physicsDirty_.clear();
}

void Scene::addParticle(const Particle &particle)
{
    const EngineSettings &settings = engineSettings();
    int capCount = static_cast<int>(std::lround(static_cast<double>(settings.maxParticles) * settings.particleDensity));
    if (capCount < 1)
        capCount = 1;
    if (capCount > 256)
        capCount = 256;
    const size_t cap = static_cast<size_t>(capCount);
    if (particles_.size() >= cap)
    {
        spdlog::warn("particle cap ({}) reached; later particles are dropped", cap);
        return;
    }
    particles_.push_back(particle);
}

entt::entity Scene::entity(EntityId id) const
{
    const auto found = entities_.find(id);
    return found == entities_.end() ? entt::null : found->second;
}

Object *Scene::create(EntityId requested)
{
    EntityId id = requested;
    if (id == kInvalidEntityId || entities_.contains(id))
    {
        EntityId counter = entityIdCounter(nextId_);
        if (counter == 0)
            counter = 1;
        EntityId candidate = makeEntityId(sessionPrefix_, counter);
        while (entities_.contains(candidate) || candidate == kInvalidEntityId)
        {
            ++counter;
            if (counter == 0)
                ++counter;
            candidate = makeEntityId(sessionPrefix_, counter);
        }
        id = candidate;
        EntityId nextCounter = counter + 1;
        if (nextCounter == 0)
            nextCounter = 1;
        nextId_ = makeEntityId(sessionPrefix_, nextCounter);
    }
    else if (entityIdPrefix(id) == sessionPrefix_)
    {
        const EntityId counter = entityIdCounter(id) + 1;
        if (counter == 0)
            nextId_ = makeEntityId(sessionPrefix_, 1);
        else if (counter > entityIdCounter(nextId_) || entityIdPrefix(nextId_) != sessionPrefix_)
            nextId_ = makeEntityId(sessionPrefix_, counter);
    }

    const entt::entity value = registry_.create();
    registry_.emplace<EntityIdComponent>(value, id);
    registry_.emplace<Transform>(value);
    registry_.emplace<Name>(value);
    registry_.emplace<Tag>(value);
    registry_.emplace<Layer>(value);
    registry_.emplace<Motion>(value);
    registry_.emplace<Action>(value);
    registry_.emplace<Spawner>(value);
    entities_[id] = value;
    order_.push_back(id);
    auto handle = std::make_unique<Object>(this, id);
    Object *raw = handle.get();
    handles_[id] = std::move(handle);
    objectView_.clear();
    bumpParentFrames();
    markPhysicsDirty(id);
    return raw;
}

Object *Scene::addSphere(const Vec3 &center, double radius, const Material &material, EntityId id)
{
    Object *object = create(id);
    registry_.emplace<SphereShape>(entity(object->id()), SphereShape{radius});
    registry_.emplace<MaterialComponent>(entity(object->id()));
    object->setLocalPosition(center);
    object->setMaterial(material);
    return object;
}

Object *Scene::addPlane(const Vec3 &point, const Vec3 &normal, const Material &material, EntityId id)
{
    Object *object = create(id);
    PlaneShape shape;
    shape.normal = length(normal) == 0 ? Vec3(0, 1, 0) : normalize(normal);
    registry_.emplace<PlaneShape>(entity(object->id()), shape);
    registry_.emplace<MaterialComponent>(entity(object->id()));
    object->setLocalPosition(point);
    object->setMaterial(material);
    return object;
}

Object *Scene::addMesh(EntityId id)
{
    Object *object = create(id);
    registry_.emplace<MeshShape>(entity(object->id()));
    registry_.emplace<MaterialComponent>(entity(object->id()));
    object->setMaterial(Material::makeDiffuse(Vec3(0.7, 0.7, 0.72)));
    return object;
}

Object *Scene::addTerrain(EntityId id)
{
    Object *object = addMesh(id);
    registry_.emplace<TerrainTileComponent>(entity(object->id()));
    object->setMaterial(Material::makeDiffuse(Vec3(0.28, 0.42, 0.22)));
    return object;
}

Object *Scene::find(EntityId id)
{
    const auto found = handles_.find(id);
    return found == handles_.end() ? nullptr : found->second.get();
}

const Object *Scene::find(EntityId id) const
{
    const auto found = handles_.find(id);
    return found == handles_.end() ? nullptr : found->second.get();
}

bool Scene::remove(EntityId id)
{
    const auto found = entities_.find(id);
    if (found == entities_.end())
        return false;
    registry_.destroy(found->second);
    entities_.erase(found);
    handles_.erase(id);
    std::erase(order_, id);
    objectView_.clear();
    bumpParentFrames();
    markPhysicsDirty(id);
    return true;
}

const std::vector<Object *> &Scene::objects() const
{
    if (objectView_.size() != order_.size())
    {
        objectView_.clear();
        objectView_.reserve(order_.size());
        for (EntityId id : order_)
            objectView_.push_back(handles_.at(id).get());
    }
    return objectView_;
}

Scene Scene::clone() const
{
    Scene copy;
    copy.lights_ = lights_;
    copy.shots_ = shots_;
    copy.ambient_ = ambient_;
    copy.horizon_ = horizon_;
    copy.zenith_ = zenith_;
    copy.environment_ = environment_;
    copy.exposure_ = exposure_;
    copy.fogColor_ = fogColor_;
    copy.fogDensity_ = fogDensity_;
    copy.map_ = map_;
    copy.liquids_ = liquids_;
    for (const Object *object : objects())
        object->clone(copy);
    copy.sessionPrefix_ = sessionPrefix_;
    copy.nextId_ = nextId_;
    return copy;
}

Scene::Scene(Scene &&other) noexcept
    : sessionPrefix_(other.sessionPrefix_), nextId_(other.nextId_), registry_(std::move(other.registry_)),
      entities_(std::move(other.entities_)), order_(std::move(other.order_)),
      physicsDirty_(std::move(other.physicsDirty_)),
      lights_(std::move(other.lights_)), shots_(std::move(other.shots_)),
      particles_(std::move(other.particles_)), ambient_(other.ambient_),
      horizon_(other.horizon_), zenith_(other.zenith_),
      environment_(std::move(other.environment_)), exposure_(other.exposure_),
      fogColor_(other.fogColor_), fogDensity_(other.fogDensity_),
      map_(std::move(other.map_)), liquids_(std::move(other.liquids_)),
      parentFrameEpoch_(other.parentFrameEpoch_)
{
    rebuildHandles();
}

Scene &Scene::operator=(Scene &&other) noexcept
{
    if (this == &other)
        return *this;
    sessionPrefix_ = other.sessionPrefix_;
    nextId_ = other.nextId_;
    registry_ = std::move(other.registry_);
    entities_ = std::move(other.entities_);
    order_ = std::move(other.order_);
    physicsDirty_ = std::move(other.physicsDirty_);
    lights_ = std::move(other.lights_);
    shots_ = std::move(other.shots_);
    particles_ = std::move(other.particles_);
    ambient_ = other.ambient_;
    horizon_ = other.horizon_;
    zenith_ = other.zenith_;
    environment_ = std::move(other.environment_);
    exposure_ = other.exposure_;
    fogColor_ = other.fogColor_;
    fogDensity_ = other.fogDensity_;
    map_ = std::move(other.map_);
    liquids_ = std::move(other.liquids_);
    parentFrameEpoch_ = other.parentFrameEpoch_;
    rebuildHandles();
    return *this;
}

void Scene::rebuildHandles()
{
    handles_.clear();
    objectView_.clear();
    for (EntityId id : order_)
        handles_[id] = std::make_unique<Object>(this, id);
    bumpParentFrames();
}

EntityId Object::parentId() const { return component<Transform>(*this).parent; }
void Object::notifyTransformChanged() const
{
    if (!scene_)
        return;
    scene_->bumpParentFrames();
    scene_->markPhysicsDirty(id_);
}
void Object::setParentId(EntityId parent) { component<Transform>(*this).parent = parent; scene_->bumpParentFrames(); }
std::string &Object::name() { return component<Name>(*this).value; }
const std::string &Object::name() const { return component<Name>(*this).value; }
std::string &Object::tag() { return component<Tag>(*this).value; }
const std::string &Object::tag() const { return component<Tag>(*this).value; }
int &Object::layer() { return component<Layer>(*this).value; }
int Object::layer() const { return component<Layer>(*this).value; }
Motion &Object::motion() { return component<Motion>(*this); }
const Motion &Object::motion() const { return component<Motion>(*this); }
Action &Object::action() { return component<Action>(*this); }
const Action &Object::action() const { return component<Action>(*this); }
double &Object::spawnEvery() { return component<Spawner>(*this).every; }
double Object::spawnEvery() const { return component<Spawner>(*this).every; }
int &Object::spawnClock() { return component<Spawner>(*this).clock; }
int Object::spawnClock() const { return component<Spawner>(*this).clock; }
EntityId &Object::spawnedId() { return component<Spawner>(*this).spawnedId; }
EntityId Object::spawnedId() const { return component<Spawner>(*this).spawnedId; }

std::string &Object::prefab()
{
    auto &registry = scene_->registry();
    const entt::entity value = scene_->entity(id_);
    return registry.get_or_emplace<PrefabSource>(value).name;
}
const std::string &Object::prefab() const
{
    static const std::string empty;
    const auto *value = scene_->registry().try_get<PrefabSource>(scene_->entity(id_));
    return value ? value->name : empty;
}
std::string &Object::instanceOf()
{
    auto &registry = scene_->registry();
    const entt::entity value = scene_->entity(id_);
    return registry.get_or_emplace<PrefabInstance>(value).source;
}
const std::string &Object::instanceOf() const
{
    static const std::string empty;
    const auto *value = scene_->registry().try_get<PrefabInstance>(scene_->entity(id_));
    return value ? value->source : empty;
}

void Object::setTag(const std::string &value)
{
    Tag &entry = component<Tag>(*this);
    entry.value = value;
    entry.role = makeRole(value);
    scene_->markPhysicsDirty(id_);
}

void Object::ensureRole() const
{
    Tag &entry = const_cast<Tag &>(component<Tag>(*this));
    if (!entry.role || entry.value != entry.role->fileTag())
        entry.role = makeRole(entry.value);
}

std::shared_ptr<Role> Object::role() const
{
    ensureRole();
    return component<Tag>(*this).role;
}

const char *Object::kind() const
{
    if (isSphere()) return "Sphere";
    if (isPlane()) return "Plane";
    if (isTerrain()) return "Terrain";
    if (isMesh()) return "Mesh";
    return "Object";
}

bool Object::isSphere() const { return scene_->registry().all_of<SphereShape>(scene_->entity(id_)); }
bool Object::isPlane() const { return scene_->registry().all_of<PlaneShape>(scene_->entity(id_)); }
bool Object::isMesh() const { return scene_->registry().all_of<MeshShape>(scene_->entity(id_)); }
bool Object::isTerrain() const { return scene_->registry().all_of<TerrainTileComponent>(scene_->entity(id_)); }
Material Object::material() const
{
    const auto *material = scene_->registry().try_get<MaterialComponent>(scene_->entity(id_));
    return material ? material->material : Material{};
}
void Object::setMaterial(const Material &value)
{
    auto *material = scene_->registry().try_get<MaterialComponent>(scene_->entity(id_));
    if (material == nullptr)
        material = &scene_->registry().emplace<MaterialComponent>(scene_->entity(id_));
    material->material = value;
}

Vec3 Object::localPosition() const { return toVec3(component<Transform>(*this).position); }
glm::dquat Object::localRotationQuat() const { return component<Transform>(*this).rotation; }
Vec3 Object::localRotation() const { return eulerDegreesFromQuat(localRotationQuat()); }
glm::dvec3 Object::localScaleVec() const { return component<Transform>(*this).scale; }
double Object::localScale() const { return maxAxisScale(localScaleVec()); }

void Object::setLocalPosition(const Vec3 &position)
{
    Transform &transform = component<Transform>(*this);
    transform.position = toGlm(position);
    markTransformDirty(transform);
    notifyTransformChanged();
}

void Object::setLocalRotation(const Vec3 &degrees)
{
    setLocalRotationQuat(quatFromEulerDegrees(degrees));
}

void Object::setLocalRotationQuat(const glm::dquat &rotation)
{
    Transform &transform = component<Transform>(*this);
    transform.rotation = glm::normalize(rotation);
    markTransformDirty(transform);
    notifyTransformChanged();
}

void Object::setLocalScale(double scale)
{
    const double placed = scale > 0 ? scale : 0.01;
    setLocalScaleVec(glm::dvec3(placed));
}

void Object::setLocalScaleVec(const glm::dvec3 &scale)
{
    Transform &transform = component<Transform>(*this);
    transform.scale = glm::dvec3(scale.x > 0 ? scale.x : 0.01, scale.y > 0 ? scale.y : 0.01, scale.z > 0 ? scale.z : 0.01);
    markTransformDirty(transform);
    notifyTransformChanged();
}

glm::dmat4 Object::worldMatrix() const
{
    Transform &transform = const_cast<Transform &>(component<Transform>(*this));
    const std::uint32_t epoch = scene_->parentFrameEpoch();
    if (!transform.matricesDirty && transform.worldEpoch == epoch)
        return transform.worldMatrix;

    // Every shape inherits the complete parent matrix. Sphere and plane children
    // therefore rotate and scale with parents just like mesh children.
    glm::dmat4 parent(1);
    std::unordered_set<EntityId> seen;
    std::vector<const Object *> chain;
    const Object *cursor = this;
    while (cursor && cursor->parentId() != kInvalidEntityId && chain.size() < 64)
    {
        if (!seen.insert(cursor->id()).second)
            break;
        cursor = scene_->find(cursor->parentId());
        if (cursor)
            chain.push_back(cursor);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        parent *= localMatrixOf(component<Transform>(**it));
    transform.localMatrix = localMatrixOf(transform);
    transform.worldMatrix = parent * transform.localMatrix;
    transform.matricesDirty = false;
    transform.worldEpoch = epoch;
    return transform.worldMatrix;
}

Vec3 Object::worldPosition() const { return transformPoint(worldMatrix(), Vec3()); }

glm::dmat4 Object::displayWorldMatrix() const
{
    const DisplayTransform *display = scene_->registry().try_get<DisplayTransform>(scene_->entity(id_));
    if (display == nullptr || !display->active)
        return worldMatrix();

    glm::dmat4 parent(1);
    std::unordered_set<EntityId> seen;
    std::vector<const Object *> chain;
    const Object *cursor = this;
    while (cursor && cursor->parentId() != kInvalidEntityId && chain.size() < 64)
    {
        if (!seen.insert(cursor->id()).second)
            break;
        cursor = scene_->find(cursor->parentId());
        if (cursor)
            chain.push_back(cursor);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const DisplayTransform *parentDisplay = scene_->registry().try_get<DisplayTransform>(scene_->entity((*it)->id()));
        if (parentDisplay != nullptr && parentDisplay->active)
            parent *= localMatrixOf(*parentDisplay);
        else
            parent *= localMatrixOf(component<Transform>(**it));
    }
    return parent * localMatrixOf(*display);
}

Vec3 Object::displayWorldPosition() const { return transformPoint(displayWorldMatrix(), Vec3()); }
double Object::displayWorldScale() const { return matrixMaxAxisScale(displayWorldMatrix()); }
double Object::displayWorldRadius() const { return radius() * displayWorldScale(); }
Vec3 Object::displayWorldNormal() const
{
    const glm::dvec4 turned = glm::transpose(glm::inverse(displayWorldMatrix())) * glm::dvec4(normal().x, normal().y, normal().z, 0);
    const Vec3 result(turned.x, turned.y, turned.z);
    return length(result) > 1e-8 ? normalize(result) : Vec3(0, 1, 0);
}

void Object::clearDisplayPose()
{
    DisplayTransform *display = scene_->registry().try_get<DisplayTransform>(scene_->entity(id_));
    if (display != nullptr)
        display->active = false;
}

void Object::setDisplayPose(const glm::dvec3 &position, const glm::dquat &rotation, const glm::dvec3 &scale)
{
    DisplayTransform *display = scene_->registry().try_get<DisplayTransform>(scene_->entity(id_));
    if (display == nullptr)
        display = &scene_->registry().emplace<DisplayTransform>(scene_->entity(id_));
    display->position = position;
    display->rotation = rotation;
    display->scale = scale;
    display->active = true;
}

void Object::setWorldPosition(const Vec3 &position)
{
    glm::dmat4 parent(1);
    if (const Object *value = scene_->find(parentId()))
        parent = value->worldMatrix();
    const glm::dvec4 local = glm::inverse(parent) * glm::dvec4(position.x, position.y, position.z, 1);
    setLocalPosition(Vec3(local.x, local.y, local.z));
}

double Object::worldScale() const { return matrixMaxAxisScale(worldMatrix()); }
Vec3 Object::worldScaleVec() const { return matrixAxisScales(worldMatrix()); }
double Object::bodyRadius() const { return isSphere() ? worldRadius() : 0; }
double Object::radius() const { const auto *shape = scene_->registry().try_get<SphereShape>(scene_->entity(id_)); return shape ? shape->radius : 0; }
double Object::worldRadius() const { return radius() * worldScale(); }
void Object::setRadius(double value)
{
    if (auto *shape = scene_->registry().try_get<SphereShape>(scene_->entity(id_)))
    {
        shape->radius = value;
        scene_->markPhysicsDirty(id_);
    }
}

Vec3 Object::normal() const
{
    const auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_));
    return shape ? shape->normal : Vec3(0, 1, 0);
}
Vec3 Object::worldNormal() const
{
    const glm::dvec4 turned = glm::transpose(glm::inverse(worldMatrix())) * glm::dvec4(normal().x, normal().y, normal().z, 0);
    const Vec3 result(turned.x, turned.y, turned.z);
    return length(result) > 1e-8 ? normalize(result) : Vec3(0, 1, 0);
}
void Object::setNormal(const Vec3 &value)
{
    if (auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_)))
    {
        shape->normal = length(value) == 0 ? Vec3(0, 1, 0) : normalize(value);
        scene_->markPhysicsDirty(id_);
    }
}
bool Object::checker() const { const auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_)); return shape && shape->checker; }
Object &Object::setChecker(const Vec3 &albedo, double scale)
{
    if (auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_)))
    {
        shape->checker = true;
        shape->checkerAlbedo = albedo;
        shape->checkerScale = scale > 0 ? scale : 1;
    }
    return *this;
}
void Object::setCheckerEnabled(bool enabled) { if (auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_))) shape->checker = enabled; }
Vec3 Object::checkerAlbedo() const { const auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_)); return shape ? shape->checkerAlbedo : Vec3(); }
void Object::setCheckerAlbedo(const Vec3 &value) { if (auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_))) shape->checkerAlbedo = value; }
double Object::checkerScale() const { const auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_)); return shape ? shape->checkerScale : 1; }
void Object::setCheckerScale(double value) { if (auto *shape = scene_->registry().try_get<PlaneShape>(scene_->entity(id_))) shape->checkerScale = value > 0 ? value : 1; }

bool Object::intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const
{
    if (isMesh())
        return mesh_detail::intersect(*this, ray, tMin, tMax, hit);
    if (isSphere())
    {
        const double placedRadius = worldRadius();
        const Vec3 oc = ray.origin - center();
        const double a = dot(ray.direction, ray.direction);
        const double halfB = dot(oc, ray.direction);
        const double c = dot(oc, oc) - placedRadius * placedRadius;
        const double discriminant = halfB * halfB - a * c;
        if (discriminant < 0 || a == 0)
            return false;
        const double root = std::sqrt(discriminant);
        double t = (-halfB - root) / a;
        if (t < tMin || t > tMax)
        {
            t = (-halfB + root) / a;
            if (t < tMin || t > tMax) return false;
        }
        hit.t = t;
        hit.point = ray.at(t);
        const Vec3 outward = normalize(hit.point - center());
        hit.normal = dot(ray.direction, outward) < 0 ? outward : -outward;
        hit.material = material();
        return true;
    }
    if (isPlane())
    {
        const Vec3 placedNormal = worldNormal();
        const double denom = dot(ray.direction, placedNormal);
        if (std::abs(denom) < 1e-8) return false;
        const double t = dot(point() - ray.origin, placedNormal) / denom;
        if (t < tMin || t > tMax) return false;
        hit.t = t;
        hit.point = ray.at(t);
        hit.normal = denom < 0 ? placedNormal : -placedNormal;
        hit.material = material();
        if (checker())
        {
            const int ix = static_cast<int>(std::floor(hit.point.x / checkerScale()));
            const int iz = static_cast<int>(std::floor(hit.point.z / checkerScale()));
            if (((ix + iz) & 1) != 0) hit.material.albedo = checkerAlbedo();
        }
        return true;
    }
    return false;
}

Hit Object::contactSphere(const Vec3 &value, double size) const
{
    if (isSphere()) return sphereHitSphere(value, size, center(), worldRadius());
    if (isPlane()) return sphereHitPlane(value, size, point(), worldNormal());
    if (isMesh()) return mesh_detail::contactSphere(*this, value, size);
    return {};
}
bool Object::overlapsSphere(const Vec3 &value, double size) const { return contactSphere(value, size).hit; }
bool Object::blocksPlayer(EntityId playerId) const
{
    if (id_ == playerId) return false;
    const auto value = role();
    if (value && value->passThroughSolid()) return false;
    return !isMesh() || (value ? value->meshBlocks() : tag() == "solid" || tag() == "platform" || tag().empty());
}
bool Object::blocksCamera(EntityId playerId) const { return layer() == 0 && blocksPlayer(playerId); }
ColliderSketch Object::colliderSketch() const
{
    if (isMesh()) return mesh_detail::colliderSketch(*this);
    ColliderSketch sketch;
    if (isSphere())
    {
        sketch.kind = ColliderSketch::Kind::Sphere;
        sketch.center = center();
        sketch.radius = worldRadius();
        const auto value = role();
        sketch.player = value && value->kind() == RoleKind::Player;
    }
    else if (isPlane())
    {
        sketch.kind = ColliderSketch::Kind::PlanePatch;
        sketch.center = point();
        sketch.normal = worldNormal();
    }
    return sketch;
}

bool Object::copyShapeFrom(const Object &source)
{
    if (isSphere() && source.isSphere()) { setRadius(source.radius()); return true; }
    if (isPlane() && source.isPlane()) { setNormal(source.normal()); return true; }
    if (isMesh() && source.isMesh())
    {
        component<MeshShape>(*this) = component<MeshShape>(source);
        if (source.isTerrain())
            scene_->registry().emplace_or_replace<TerrainTileComponent>(scene_->entity(id_),
                component<TerrainTileComponent>(source));
        else
        {
            if (scene_->registry().all_of<TerrainTileComponent>(scene_->entity(id_)))
                scene_->registry().remove<TerrainTileComponent>(scene_->entity(id_));
        }
        setLocalRotationQuat(source.localRotationQuat());
        setLocalScaleVec(source.localScaleVec());
        return true;
    }
    return false;
}

void Object::mixShapeHash(std::uint64_t &hash) const
{
    if (isMesh()) { mesh_detail::mixShapeHash(*this, hash); return; }
    mixBits(hash, isSphere() ? 1 : 2);
    mixVec(hash, worldPosition());
    if (isSphere())
    {
        mixDouble(hash, worldRadius());
        mixDouble(hash, radius());
    }
    else
    {
        mixVec(hash, normal());
        mixBits(hash, checker() ? 1 : 0);
        mixVec(hash, checkerAlbedo());
        mixDouble(hash, checkerScale());
    }
}

EntityId Object::clone(Scene &destination) const
{
    Object *copy = isSphere() ? destination.addSphere(localPosition(), radius(), material(), id_)
                 : isPlane() ? destination.addPlane(localPosition(), normal(), material(), id_)
                 : isTerrain() ? destination.addTerrain(id_)
                             : destination.addMesh(id_);
    if (isMesh())
        component<MeshShape>(*copy) = component<MeshShape>(*this);
    if (isTerrain())
        component<TerrainTileComponent>(*copy) = component<TerrainTileComponent>(*this);
    copy->setLocalPosition(localPosition());
    copy->name() = name();
    copy->setTag(tag());
    copy->layer() = layer();
    copy->prefab() = prefab();
    copy->instanceOf() = instanceOf();
    copy->motion() = motion();
    copy->action() = action();
    copy->spawnEvery() = spawnEvery();
    copy->spawnClock() = spawnClock();
    copy->spawnedId() = spawnedId();
    copy->setParentId(parentId());
    copy->setLocalRotationQuat(localRotationQuat());
    copy->setLocalScaleVec(localScaleVec());
    if (isPlane())
    {
        copy->setCheckerEnabled(checker());
        copy->setCheckerAlbedo(checkerAlbedo());
        copy->setCheckerScale(checkerScale());
    }
    return copy->id();
}

const std::shared_ptr<MeshGeometry> &Object::geometry() const
{
    static const std::shared_ptr<MeshGeometry> empty;
    const auto *shape = scene_->registry().try_get<MeshShape>(scene_->entity(id_));
    return shape ? shape->geometry : empty;
}
const std::vector<MeshTri> &Object::triangles() const
{
    static const std::vector<MeshTri> empty;
    return geometry() ? geometry()->triangles : empty;
}
const std::string &Object::sourcePath() const
{
    static const std::string empty;
    const auto *shape = scene_->registry().try_get<MeshShape>(scene_->entity(id_));
    return shape ? shape->sourcePath : empty;
}
void Object::setTriangles(std::vector<MeshTri> triangles)
{
    auto geometryValue = std::make_shared<MeshGeometry>();
    meshAssignTriangles(*geometryValue, std::move(triangles));
    component<MeshShape>(*this).geometry = std::move(geometryValue);
}
bool Object::loadMesh(const std::filesystem::path &path, std::string &error)
{
    if (isRttPath(path))
        return loadTerrain(path, error);
    auto loaded = loadMeshFile(path, error);
    if (!loaded) return false;
    if (scene_->registry().all_of<TerrainTileComponent>(scene_->entity(id_)))
        scene_->registry().remove<TerrainTileComponent>(scene_->entity(id_));
    MeshShape &shape = component<MeshShape>(*this);
    shape.geometry = std::move(loaded);
    const std::u8string bytes = path.u8string();
    shape.sourcePath.assign(bytes.begin(), bytes.end());
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    shape.sourceStamp = ec ? 0 : static_cast<std::int64_t>(stamp.time_since_epoch().count());
    shape.sourceBytes = ec ? 0 : std::filesystem::file_size(path, ec);
    return true;
}

const std::shared_ptr<TerrainTile> &Object::terrainTile() const
{
    static const std::shared_ptr<TerrainTile> empty;
    const auto *shape = scene_->registry().try_get<TerrainTileComponent>(scene_->entity(id_));
    return shape ? shape->tile : empty;
}

bool Object::loadTerrain(const std::filesystem::path &path, std::string &error)
{
    auto tile = loadTerrainFile(path, error);
    if (!tile)
        return false;
    auto geometry = std::make_shared<MeshGeometry>();
    terrainBuildMesh(*tile, *geometry);
    geometry->sourcePath = tile->sourcePath;
    MeshShape &shape = component<MeshShape>(*this);
    shape.geometry = std::move(geometry);
    shape.sourcePath = tile->sourcePath;
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    shape.sourceStamp = ec ? 0 : static_cast<std::int64_t>(stamp.time_since_epoch().count());
    shape.sourceBytes = ec ? 0 : std::filesystem::file_size(path, ec);
    scene_->registry().emplace_or_replace<TerrainTileComponent>(scene_->entity(id_), TerrainTileComponent{std::move(tile)});
    return true;
}

void Object::setTerrainData(std::shared_ptr<TerrainTile> tile, std::shared_ptr<MeshGeometry> mesh)
{
    if (!tile)
        return;
    MeshShape &shape = component<MeshShape>(*this);
    if (mesh)
        shape.geometry = std::move(mesh);
    else
    {
        shape.geometry = std::make_shared<MeshGeometry>();
        finishGeometry(*shape.geometry);
    }
    shape.sourcePath = tile->sourcePath;
    scene_->registry().emplace_or_replace<TerrainTileComponent>(scene_->entity(id_), TerrainTileComponent{std::move(tile)});
}
bool Object::refreshFromDisk(std::string &error)
{
    MeshShape &shape = component<MeshShape>(*this);
    if (isTerrain() || isRttPath(shape.sourcePath))
        return loadTerrain(shape.sourcePath, error);
    return refreshMeshFile(shape.geometry, shape.sourcePath, shape.sourceStamp, shape.sourceBytes, error);
}
void Object::worldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const { worldAxesFromMatrix(worldMatrix(), axisX, axisY, axisZ); }
void Object::displayWorldAxes(Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ) const
{
    worldAxesFromMatrix(displayWorldMatrix(), axisX, axisY, axisZ);
}

void Object::writeScene(std::ostream &out) const
{
    if (isTerrain())
    {
        const Vec3 position = localPosition();
        const Vec3 rotation = localRotation();
        out << "terrain \"" << scene_write::escapeName(name()) << "\" "
            << position.x << ' ' << position.y << ' ' << position.z << ' '
            << localScale() << ' ';
        scene_write::writeMaterial(out, material());
        if (meshRotationActive(rotation))
            out << " rot " << rotation.x << ' ' << rotation.y << ' ' << rotation.z;
        out << " rtt \"" << scene_write::escapeName(sourcePath()) << '"';
        scene_write::writeObjectTail(out, *this);
        out << '\n';
        return;
    }
    if (isMesh())
    {
        mesh_detail::writeScene(*this, out);
        return;
    }
    const Vec3 position = localPosition();
    if (isSphere())
    {
        out << "sphere \"" << scene_write::escapeName(name()) << "\" "
            << position.x << ' ' << position.y << ' ' << position.z << ' '
            << radius() << ' ';
        scene_write::writeMaterial(out, material());
    }
    else if (isPlane())
    {
        const Vec3 localNormal = normal();
        out << "plane \"" << scene_write::escapeName(name()) << "\" "
            << position.x << ' ' << position.y << ' ' << position.z << ' '
            << localNormal.x << ' ' << localNormal.y << ' ' << localNormal.z << ' ';
        scene_write::writeMaterial(out, material());
        if (checker())
        {
            const Vec3 albedo = checkerAlbedo();
            out << " checker " << albedo.x << ' ' << albedo.y << ' ' << albedo.z << ' ' << checkerScale();
        }
    }
    scene_write::writeObjectTail(out, *this);
    out << '\n';
}

