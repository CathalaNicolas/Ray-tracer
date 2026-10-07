#include <doctest/doctest.h>

#include "EngineSettings.hpp"
#include "Frustum.hpp"
#include "GpuLights.hpp"
#include "Material.hpp"
#include "Scene.hpp"

TEST_CASE("frustum keeps a box in front of the camera and rejects one behind")
{
    const Camera camera(Vec3(0, 0, 8), Vec3(0, 0, 0), Vec3(0, 1, 0), 50, 1);
    const Frustum frustum = frustumFromCamera(camera, 0.02, 100);
    CHECK_FALSE(frustumAabbOutside(frustum, Vec3(-0.5, -0.5, -0.5), Vec3(0.5, 0.5, 0.5)));
    CHECK(frustumAabbOutside(frustum, Vec3(-0.5, -0.5, 20), Vec3(0.5, 0.5, 21)));
    CHECK_FALSE(frustumSphereOutside(frustum, Vec3(0, 0, 0), 0.5));
    CHECK(frustumSphereOutside(frustum, Vec3(0, 0, 40), 0.5));
}

TEST_CASE("view distance uses the closest AABB point")
{
    CHECK_FALSE(aabbBeyondDistance(Vec3(-1, -1, -1), Vec3(1, 1, 1), Vec3(0, 0, 0), 0.5));
    CHECK(aabbBeyondDistance(Vec3(10, 0, 0), Vec3(12, 1, 1), Vec3(0, 0, 0), 5));
    CHECK_FALSE(sphereBeyondDistance(Vec3(3, 0, 0), 1, Vec3(0, 0, 0), 2.5));
    CHECK(sphereBeyondDistance(Vec3(8, 0, 0), 1, Vec3(0, 0, 0), 5));
}

TEST_CASE("light pick keeps directional lights and the nearest bright points")
{
    Scene scene;
    PointLight farFill(Vec3(100, 4, 0), Vec3(1, 1, 1), 0.2, 0.05);
    PointLight nearKey(Vec3(1, 4, 0), Vec3(1, 1, 1), 1.5, 0.02);
    PointLight sun(Vec3(0, 20, 0), Vec3(1, 1, 1), 0.4, 0);
    sun.directional = true;
    scene.addLight(farFill);
    scene.addLight(nearKey);
    scene.addLight(sun);
    Object *lamp = scene.addSphere(Vec3(2, 1, 0), 0.2, Material::makeDiffuse(Vec3(1, 0.8, 0.4)));
    Material emit = lamp->material();
    emit.emission = 2;
    lamp->setMaterial(emit);
    const std::vector<GpuLightPick> picked = rankGpuLights(scene, Vec3(0, 0, 0), 2);
    REQUIRE(picked.size() == 2);
    CHECK_FALSE(picked[0].fromObject);
    CHECK(picked[0].index == 2);
    CHECK_FALSE(picked[1].fromObject);
    CHECK(picked[1].index == 1);
}

TEST_CASE("quality settings persist through the settings file keys")
{
    EngineSettings &settings = engineSettings();
    const double oldDistance = settings.viewDistance;
    const int oldShadow = settings.shadowMapSize;
    const double oldDensity = settings.particleDensity;
    CHECK(applyEngineSetting("view_distance", "250"));
    CHECK(applyEngineSetting("shadow_map", "512"));
    CHECK(applyEngineSetting("particle_density", "0.5"));
    CHECK(settings.viewDistance == doctest::Approx(250));
    CHECK(settings.shadowMapSize == 512);
    CHECK(settings.particleDensity == doctest::Approx(0.5));
    settings.viewDistance = oldDistance;
    settings.shadowMapSize = oldShadow;
    settings.particleDensity = oldDensity;
}
