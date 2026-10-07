#pragma once

#include "EngineSettings.hpp"
#include "Hittable.hpp"
#include "Light.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
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
int placePrefabInstance(Scene &scene, int sourceId);

class Scene
{
public:
    Scene() = default;
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;
    Scene(Scene &&other) noexcept;
    Scene &operator=(Scene &&other) noexcept;

    int add(std::unique_ptr<Hittable> object)
    {
        if (!object)
            return -1;
        if (object->id <= 0)
            object->id = nextId_++;
        else if (object->id >= nextId_)
            nextId_ = object->id + 1;
        int id = object->id;
        object->bindScene(this);
        Hittable *raw = object.get();
        objects_.push_back(std::move(object));
        byId_[id] = raw;
        bumpParentFrames();
        return id;
    }

    Hittable *find(int id)
    {
        const auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : it->second;
    }

    const Hittable *find(int id) const
    {
        const auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : it->second;
    }

    bool remove(int id)
    {
        for (auto it = objects_.begin(); it != objects_.end(); ++it)
        {
            if ((*it)->id == id)
            {
                byId_.erase(id);
                objects_.erase(it);
                bumpParentFrames();
                return true;
            }
        }
        return false;
    }

    // Invalidate every object's parentFrame() cache. Call after poses or parents change.
    void bumpParentFrames() const
    {
        ++parentFrameEpoch_;
        if (parentFrameEpoch_ == 0)
            parentFrameEpoch_ = 1;
    }

    std::uint32_t parentFrameEpoch() const { return parentFrameEpoch_; }

    Scene clone() const
    {
        Scene copy;
        copy.lights_ = lights_;
        copy.ambient_ = ambient_;
        copy.horizon_ = horizon_;
        copy.zenith_ = zenith_;
        copy.environment_ = environment_;
        copy.exposure_ = exposure_;
        copy.fogColor_ = fogColor_;
        copy.fogDensity_ = fogDensity_;
        copy.shots_ = shots_;
        copy.nextId_ = nextId_;
        for (const auto &object : objects_)
        {
            copy.objects_.push_back(object->clone());
            copy.objects_.back()->bindScene(&copy);
        }
        copy.rebuildIndex();
        return copy;
    }

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
        for (const auto &object : objects_)
        {
            HitRecord candidate;
            if (object->intersect(ray, tMin, closest, candidate))
            {
                candidate.objectId = object->id;
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

    const std::vector<std::unique_ptr<Hittable>> &objects() const { return objects_; }
    std::vector<PointLight> &lights() { return lights_; }
    const std::vector<PointLight> &lights() const { return lights_; }
    std::vector<SceneCamera> &shots() { return shots_; }
    const std::vector<SceneCamera> &shots() const { return shots_; }
    std::vector<Particle> &particles() { return particles_; }
    const std::vector<Particle> &particles() const { return particles_; }
    void addParticle(const Particle &particle)
    {
        const size_t cap = static_cast<size_t>(engineSettings().maxParticles);
        if (particles_.size() >= cap)
        {
            static bool warned = false;
            if (!warned)
            {
                std::cerr << "particle cap (" << cap << ") reached; later particles are dropped\n";
                warned = true;
            }
            return;
        }
        particles_.push_back(particle);
    }
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

private:
    int nextId_ = 1;
    std::vector<std::unique_ptr<Hittable>> objects_;
    std::unordered_map<int, Hittable *> byId_;
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
    mutable std::uint32_t parentFrameEpoch_ = 1;

    void rebuildIndex()
    {
        byId_.clear();
        byId_.reserve(objects_.size());
        for (const auto &object : objects_)
            byId_[object->id] = object.get();
    }

    void adopt()
    {
        for (const auto &object : objects_)
            object->bindScene(this);
        rebuildIndex();
        bumpParentFrames();
    }
};

inline Scene::Scene(Scene &&other) noexcept
    : nextId_(other.nextId_),
      objects_(std::move(other.objects_)),
      lights_(std::move(other.lights_)),
      ambient_(other.ambient_),
      horizon_(other.horizon_),
      zenith_(other.zenith_),
      environment_(std::move(other.environment_)),
      exposure_(other.exposure_),
      fogColor_(other.fogColor_),
      fogDensity_(other.fogDensity_),
      shots_(std::move(other.shots_)),
      particles_(std::move(other.particles_))
{
    other.byId_.clear();
    adopt();
}

inline Scene &Scene::operator=(Scene &&other) noexcept
{
    if (this != &other)
    {
        nextId_ = other.nextId_;
        objects_ = std::move(other.objects_);
        lights_ = std::move(other.lights_);
        ambient_ = other.ambient_;
        horizon_ = other.horizon_;
        zenith_ = other.zenith_;
        environment_ = std::move(other.environment_);
        exposure_ = other.exposure_;
        fogColor_ = other.fogColor_;
        fogDensity_ = other.fogDensity_;
        shots_ = std::move(other.shots_);
        particles_ = std::move(other.particles_);
        other.byId_.clear();
        adopt();
    }
    return *this;
}
