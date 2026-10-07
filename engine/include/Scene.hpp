#pragma once

#include "EngineSettings.hpp"
#include "Object.hpp"
#include "Light.hpp"
#include "Terrain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Particle
{
    Vec3 position;
    Vec3 velocity;
    Vec3 color{1, 0.85, 0.3};
    double life = 0.4;
    double size = 0.08;
};

struct SceneCamera
{
    std::string name;
    Vec3 lookFrom{0.15, 1.55, 5.5};
    Vec3 lookAt{0.0, 0.7, 0.15};
    double fov = 42;
};

void syncPrefabInstances(Scene &scene);
EntityId placePrefabInstance(Scene &scene, EntityId sourceId);

class Scene
{
public:
    Scene();
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    Scene(Scene &&other) noexcept;
    Scene &operator=(Scene &&other) noexcept;

    Object *addSphere(const Vec3 &center, double radius, const Material &material, EntityId id = kInvalidEntityId);
    Object *addPlane(const Vec3 &point, const Vec3 &normal, const Material &material, EntityId id = kInvalidEntityId);
    Object *addMesh(EntityId id = kInvalidEntityId);
    Object *addTerrain(EntityId id = kInvalidEntityId);
    Object *find(EntityId id);
    const Object *find(EntityId id) const;
    bool remove(EntityId id);

    // Invalidate every object's parentFrame() cache. Call after poses or parents change.
    void bumpParentFrames() const
    {
        ++parentFrameEpoch_;
        if (parentFrameEpoch_ == 0)
            parentFrameEpoch_ = 1;
    }

    std::uint32_t parentFrameEpoch() const { return parentFrameEpoch_; }

    Scene clone() const;

    void addLight(const PointLight &light)
    {
        lights_.push_back(light);
    }

    void setAmbient(const Vec3 &ambient)
    {
        ambient_ = ambient;
    }

    void setBackground(const Vec3 &horizon, const Vec3 &zenith)
    {
        horizon_ = horizon;
        zenith_ = zenith;
    }

    void setEnvironment(const std::string &path)
    {
        environment_ = path;
    }

    void setFog(const Vec3 &color, double density)
    {
        fogColor_ = color;
        fogDensity_ = density > 0 ? density : 0;
    }

    void setExposure(double exposure)
    {
        exposure_ = exposure > 0 ? exposure : 0;
    }

    bool intersect(const Ray &ray, double tMin, double tMax, HitRecord &hit) const
    {
        bool found = false;
        double closest = tMax;
        for (const Object *object : objects())
        {
            HitRecord candidate;
            if (object->intersect(ray, tMin, closest, candidate))
            {
                candidate.objectId = object->id();
                found = true;
                closest = candidate.t;
                hit = candidate;
            }
        }
        return found;
    }

    Vec3 background(const Ray &ray) const
    {
        double t = 0.5 * (ray.direction.y + 1.0);
        t = std::clamp(t, 0.0, 1.0);
        return horizon_ * (1.0 - t) + zenith_ * t;
    }

    const std::vector<Object *> &objects() const;
    EntityId nextId() const { return nextId_; }
    EntityId sessionPrefix() const { return sessionPrefix_; }
    // Soft: only advances the counter when `next` shares this scene's session prefix.
    void setNextId(EntityId next);
    // Play-session restore: adopt the saved prefix and counter so later spawns match.
    void restoreIdState(EntityId next);
    // After `loadScene` moves a parsed scene in, restore this process's session issuer.
    void retainSessionIds(EntityId prefix, EntityId next);
    void markPhysicsDirty(EntityId id);
    void clearPhysicsDirty();
    const std::unordered_set<EntityId> &physicsDirty() const { return physicsDirty_; }
    entt::registry &registry() { return registry_; }
    const entt::registry &registry() const { return registry_; }
    entt::entity entity(EntityId id) const;
    std::vector<PointLight> &lights() { return lights_; }
    const std::vector<PointLight> &lights() const { return lights_; }
    std::vector<SceneCamera> &shots() { return shots_; }
    const std::vector<SceneCamera> &shots() const { return shots_; }
    std::vector<Particle> &particles() { return particles_; }
    const std::vector<Particle> &particles() const { return particles_; }
    void addParticle(const Particle &particle);
    void advanceParticles(double dt)
    {
        for (Particle &particle : particles_)
        {
            particle.position = particle.position + particle.velocity * dt;
            particle.velocity.y -= 3.0 * dt;
            particle.life -= dt;
        }
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(), [](const Particle &particle) { return particle.life <= 0; }),
            particles_.end());
    }
    void removeLight(size_t index)
    {
        if (index < lights_.size())
            lights_.erase(lights_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    const Vec3 &ambient() const { return ambient_; }
    const Vec3 &horizon() const { return horizon_; }
    const Vec3 &zenith() const { return zenith_; }
    const std::string &environment() const { return environment_; }
    double exposure() const { return exposure_; }
    const Vec3 &fogColor() const { return fogColor_; }
    double fogDensity() const { return fogDensity_; }
    MapDef &map() { return map_; }
    const MapDef &map() const { return map_; }
    std::vector<LiquidVolume> &liquids() { return liquids_; }
    const std::vector<LiquidVolume> &liquids() const { return liquids_; }

private:
    friend class Object;
    EntityId sessionPrefix_ = 0;
    EntityId nextId_ = 1;
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    std::unordered_map<EntityId, std::unique_ptr<Object>> handles_;
    std::vector<EntityId> order_;
    mutable std::vector<Object *> objectView_;
    std::unordered_set<EntityId> physicsDirty_;
    std::vector<PointLight> lights_;
    std::vector<SceneCamera> shots_;
    std::vector<Particle> particles_;
    Vec3 ambient_{0.08, 0.08, 0.09};
    Vec3 horizon_{1.0, 1.0, 1.0};
    Vec3 zenith_{0.45, 0.65, 1.0};
    std::string environment_;
    double exposure_ = 1;
    Vec3 fogColor_{0.75, 0.8, 0.85};
    double fogDensity_ = 0;
    MapDef map_;
    std::vector<LiquidVolume> liquids_;
    mutable std::uint32_t parentFrameEpoch_ = 1;

    Object *create(EntityId requested);
    void rebuildHandles();
};

