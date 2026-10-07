#include "DemoScene.hpp"
#include "EntityId.hpp"
#include "MeshGeometry.hpp"
#include "Play.hpp"
#include "SceneFile.hpp"
#include "SimSerialize.hpp"
#include "TransformMath.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace
{

bool nearly(double a, double b, double eps = 1e-8)
{
    return std::abs(a - b) <= eps;
}

bool vecNear(const Vec3 &a, const Vec3 &b, double eps = 1e-8)
{
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

} // namespace

TEST_CASE("euler degrees are the inverse of quatFromEulerDegrees")
{
    const Vec3 degrees(30, 45, 20);
    const glm::dquat q = quatFromEulerDegrees(degrees);
    const Vec3 back = eulerDegreesFromQuat(q);
    INFO("euler back " << back.x << " " << back.y << " " << back.z);
    CHECK(vecNear(back, degrees, 1e-6));

    const Vec3 sample(0.5, 0.3, -0.4);
    const Vec3 rotated = meshRotate(sample, degrees);
    const glm::dvec3 also = rotateByQuat(q, toGlm(sample));
    CHECK(nearly(rotated.x, also.x, 1e-9));
    CHECK(nearly(rotated.y, also.y, 1e-9));
    CHECK(nearly(rotated.z, also.z, 1e-9));
}

TEST_CASE("clone save load and one play frame keep a 3-axis rotation")
{
    const Material paint = Material::makeDiffuse(Vec3(0.4, 0.5, 0.6));
    Scene scene;
    Object *mesh = scene.addMesh();
    REQUIRE(mesh != nullptr);
    std::string meshError;
    REQUIRE(mesh->loadMesh("assets/door.obj", meshError));
    mesh->setLocalPosition(Vec3(1, 2, 3));
    mesh->setLocalRotation(Vec3(30, 45, 20));
    mesh->setLocalScaleVec(glm::dvec3(1.25, 0.8, 1.1));
    Object *player = scene.addSphere(Vec3(0, 2, 0), 0.5, paint);
    player->setTag("player");
    Object *ground = scene.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), paint);
    ground->setTag("solid");

    Scene cloned = scene.clone();
    REQUIRE(sceneTransformsEqual(scene, cloned));
    REQUIRE(vecNear(cloned.find(mesh->id())->localRotation(), Vec3(30, 45, 20), 1e-6));

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "raytracer-transform-roundtrip.txt";
    CameraSetup camera;
    std::string error;
    REQUIRE(saveScene(path, scene, camera, error));
    Scene loaded;
    REQUIRE(loadScene(path, loaded, camera, error));
    Object *loadedMesh = nullptr;
    for (Object *object : loaded.objects())
    {
        if (object->isMesh())
            loadedMesh = object;
    }
    REQUIRE(loadedMesh != nullptr);
    CHECK(vecNear(loadedMesh->localRotation(), Vec3(30, 45, 20), 1e-5));
    std::error_code ec;
    std::filesystem::remove(path, ec);

    Scene playScene = scene.clone();
    PlayState state;
    seedPlayRng(state);
    SimSession sim;
    sim.attach(playScene, state);
    sim.begin();
    sim.step(PlayInput{}, Vec3(0, 0, -1));
    Object *after = playScene.find(mesh->id());
    REQUIRE(after != nullptr);
    CHECK(vecNear(after->localRotation(), Vec3(30, 45, 20), 1e-6));
}

TEST_CASE("entity ids use a session prefix")
{
    Scene a;
    Scene b;
    Object *one = a.addSphere(Vec3(), 1, Material::makeDiffuse(Vec3(1, 1, 1)));
    Object *two = b.addSphere(Vec3(), 1, Material::makeDiffuse(Vec3(1, 1, 1)));
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK((one->id() >> 32) != 0);
    CHECK((two->id() >> 32) != 0);
    CHECK(one->id() != two->id());
}

TEST_CASE("new ids after a scene load keep the session prefix")
{
    Scene authored;
    Object *solid = authored.addSphere(Vec3(0, 1, 0), 0.5, Material::makeDiffuse(Vec3(1, 1, 1)));
    REQUIRE(solid != nullptr);
    const EntityId authoredId = solid->id();
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "raytracer-id-prefix-test.txt";
    CameraSetup camera;
    std::string error;
    REQUIRE(saveScene(path, authored, camera, error));

    int collisions = 0;
    for (int pair = 0; pair < 20; ++pair)
    {
        Scene left;
        Scene right;
        const EntityId leftPrefix = left.sessionPrefix();
        const EntityId rightPrefix = right.sessionPrefix();
        REQUIRE(loadScene(path, left, camera, error));
        REQUIRE(loadScene(path, right, camera, error));
        CHECK(left.sessionPrefix() == leftPrefix);
        CHECK(right.sessionPrefix() == rightPrefix);
        Object *leftNext = left.addSphere(Vec3(1, 0, 0), 0.2, Material::makeDiffuse(Vec3(1, 0, 0)));
        Object *rightNext = right.addSphere(Vec3(1, 0, 0), 0.2, Material::makeDiffuse(Vec3(1, 0, 0)));
        REQUIRE(leftNext != nullptr);
        REQUIRE(rightNext != nullptr);
        CHECK(entityIdPrefix(leftNext->id()) == leftPrefix);
        CHECK(entityIdPrefix(rightNext->id()) == rightPrefix);
        CHECK(left.find(authoredId) != nullptr);
        if (leftNext->id() == rightNext->id())
            ++collisions;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    CHECK(collisions == 0);
}
