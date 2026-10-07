#pragma once

#include "Camera.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Scene.hpp"
#include "Sphere.hpp"

#include <iostream>
#include <memory>
#include <string>

inline void addDemoTree(Scene &scene)
{
    Material bark = Material::makeDiffuse(Vec3(0.40, 0.24, 0.12));
    bark.specular = 0.05;
    bark.shininess = 16;
    bark.roughness = 0.9;
    Material leaves = Material::makeDiffuse(Vec3(0.17, 0.42, 0.14));
    leaves.specular = 0.08;
    leaves.shininess = 18;
    leaves.roughness = 0.75;

    const Vec3 root(-2.4, 0.0, -0.15);
    std::string error;
    Object *trunk = scene.addMesh();
    if (!trunk->load("assets/tree-trunk.obj", error))
    {
        std::cerr << error << "\n";
        return;
    }
    trunk->name() = "Tree trunk";
    trunk->setTag("solid");
    trunk->setMaterial(bark);
    trunk->setPosition(root);
    trunk->setScale(1.2);
    trunk->setRotation(Vec3(0, 18, 0));

    Object *crown = scene.addMesh();
    if (!crown->load("assets/tree-crown.obj", error))
    {
        std::cerr << error << "\n";
        return;
    }
    crown->name() = "Tree crown";
    crown->setTag("solid");
    crown->setMaterial(leaves);
    crown->setPosition(root + Vec3(0.0, 0.86, 0.0));
    crown->setScale(1.15);
    crown->setRotation(Vec3(0, 18, 0));
}

inline Scene createDemoScene()
{
    Scene scene;
    scene.setAmbient(Vec3(0.06, 0.06, 0.07));
    scene.setBackground(Vec3(0.85, 0.88, 0.95), Vec3(0.35, 0.55, 0.95));

    Material floor;
    floor.albedo = Vec3(0.72, 0.72, 0.74);
    floor.ambient = 0.1;
    floor.diffuse = 0.75;
    floor.specular = 0.18;
    floor.shininess = 48;
    floor.reflectivity = 0.14;

    Object *ground = scene.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), floor);
    ground->name() = "Ground";
    ground->setTag("solid");
    ground->setChecker(Vec3(0.16, 0.16, 0.18), 1.15);

    Object *red = scene.addSphere(
        Vec3(-1.15, 1.0, 0.15),
        1.0,
        Material::makeDiffuse(Vec3(0.78, 0.12, 0.1)));
    red->name() = "Red sphere";

    Object *mirror = scene.addSphere(
        Vec3(1.3, 0.75, -0.35),
        0.75,
        Material::makeMetal(Vec3(0.93, 0.93, 0.96), 0.86));
    mirror->name() = "Mirror sphere";

    Material goldMaterial = Material::makeMetal(Vec3(0.9, 0.72, 0.28), 0.42);
    goldMaterial.roughness = 0.34;
    Object *gold = scene.addSphere(Vec3(0.2, 0.4, 1.35), 0.4, goldMaterial);
    gold->name() = "Gold sphere";

    Material lampMaterial = Material::makeDiffuse(Vec3(1.0, 0.62, 0.28));
    lampMaterial.ambient = 0;
    lampMaterial.diffuse = 0;
    lampMaterial.specular = 0;
    lampMaterial.emission = 8;
    Object *lamp = scene.addSphere(Vec3(1.9, 1.35, 0.35), 0.14, lampMaterial);
    lamp->name() = "Lamp";

    Object *glass = scene.addSphere(
        Vec3(-0.05, 0.42, 2.05),
        0.38,
        Material::makeGlass(Vec3(0.92, 0.96, 1.0), 1.5));
    glass->name() = "Glass sphere";

    Object *player = scene.addSphere(
        Vec3(0, 0.5, 2.2),
        0.35,
        Material::makeDiffuse(Vec3(0.2, 0.45, 0.95)));
    player->name() = "Player";
    player->setTag("player");

    Material pickupMaterial = Material::makeDiffuse(Vec3(0.95, 0.85, 0.2));
    const Vec3 pickupCenters[] = {
        Vec3(0.4, 0.22, 1.0),
        Vec3(-0.3, 0.22, 0.2),
        Vec3(0.8, 0.22, -0.6),
    };
    for (int index = 0; index < 3; ++index)
    {
        Object *pickup = scene.addSphere(pickupCenters[index], 0.22, pickupMaterial);
        pickup->name() = "Pickup " + std::to_string(index + 1);
        pickup->setTag("pickup");
    }

    Material goalMaterial = Material::makeDiffuse(Vec3(0.3, 0.9, 0.6));
    goalMaterial.transmission = 0;
    Object *goal = scene.addSphere(Vec3(2.2, 0.4, -1.2), 0.45, goalMaterial);
    goal->name() = "Goal";
    goal->setTag("goal");

    Object *hazard = scene.addSphere(
        Vec3(-1.7, 0.3, 1.5),
        0.32,
        Material::makeDiffuse(Vec3(0.55, 0.05, 0.08)));
    hazard->name() = "Hazard";
    hazard->setTag("hazard");

    Object *door = scene.addMesh();
    std::string doorError;
    if (door->load("assets/door.obj", doorError))
    {
        door->name() = "Door";
        door->setTag("solid");
        door->layer() = 1;
        door->setPosition(Vec3(1.35, 0, 0.95));
    }
    else
    {
        scene.remove(door->id());
        std::cerr << doorError << "\n";
    }

    Object *lever = scene.addSphere(
        Vec3(1.8, 0.28, 1.7),
        0.22,
        Material::makeDiffuse(Vec3(0.55, 0.25, 0.85)));
    lever->name() = "Switch";
    lever->setTag("use");
    lever->action().target = "Door";
    lever->action().move = Vec3(0, 1.6, 0);

    Object *stepStone = scene.addSphere(
        Vec3(-1.15, 0.12, 2.55),
        0.12,
        Material::makeDiffuse(Vec3(0.45, 0.45, 0.48)));
    stepStone->name() = "Step";
    stepStone->setTag("solid");

    addDemoTree(scene);

    Object *platform = scene.addMesh();
    std::string platformError;
    if (platform->load("assets/platform.obj", platformError))
    {
        platform->name() = "Platform";
        platform->setTag("platform");
        platform->setPosition(Vec3(0.15, 0, 0.35));
        platform->motion().move = Vec3(1.6, 0, 0);
        platform->motion().period = 4;
    }
    else
    {
        scene.remove(platform->id());
        std::cerr << platformError << "\n";
    }

    Object *spawn = scene.addSphere(
        Vec3(0.45, 0.5, 2.55),
        0.08,
        Material::makeDiffuse(Vec3(0.35, 0.75, 0.85)));
    spawn->name() = "Spawn";
    spawn->setTag("spawn");

    Object *spawner = scene.addSphere(
        Vec3(-0.85, 0.0, 2.85),
        0.12,
        Material::makeDiffuse(Vec3(0.95, 0.45, 0.15)));
    spawner->name() = "Spawner";
    spawner->setTag("spawner");
    spawner->spawnEvery() = 2.5;

    scene.addLight(PointLight(Vec3(3.2, 7.5, 2.4), Vec3(1, 0.97, 0.92), 1.6, 0.012));
    scene.lights().back().name = "Key";
    scene.lights().back().radius = 0.45;
    scene.addLight(PointLight(Vec3(-3.5, 4.5, 5.0), Vec3(0.75, 0.82, 1.0), 0.55, 0.02));
    scene.lights().back().name = "Fill";
    scene.lights().back().spotDirection = Vec3(3.5, -4.0, -5.0);
    scene.lights().back().spotOuter = 38;
    scene.lights().back().spotInner = 18;
    return scene;
}

inline Scene createEastScene()
{
    Scene scene;
    scene.setAmbient(Vec3(0.06, 0.06, 0.07));
    scene.setBackground(Vec3(0.85, 0.88, 0.95), Vec3(0.35, 0.55, 0.95));

    Material floor;
    floor.albedo = Vec3(0.72, 0.72, 0.74);
    floor.ambient = 0.1;
    floor.diffuse = 0.75;
    floor.specular = 0.18;
    floor.shininess = 48;
    floor.reflectivity = 0.14;

    Object *ground = scene.addPlane(Vec3(0, 0, 0), Vec3(0, 1, 0), floor);
    ground->name() = "Ground";
    ground->setTag("solid");
    ground->setChecker(Vec3(0.16, 0.16, 0.18), 0.7);

    Object *player = scene.addSphere(
        Vec3(0, 0.5, 1.6),
        0.35,
        Material::makeDiffuse(Vec3(0.2, 0.45, 0.95)));
    player->name() = "Player";
    player->setTag("player");

    Object *pickup = scene.addSphere(
        Vec3(-1.2, 0.22, 0.4),
        0.22,
        Material::makeDiffuse(Vec3(0.95, 0.85, 0.2)));
    pickup->name() = "Pickup";
    pickup->setTag("pickup");

    Material goalMaterial = Material::makeDiffuse(Vec3(0.3, 0.9, 0.6));
    goalMaterial.transmission = 0;
    Object *goal = scene.addSphere(Vec3(1.5, 0.45, -0.8), 0.45, goalMaterial);
    goal->name() = "Goal";
    goal->setTag("goal");
    return scene;
}

struct CameraSetup
{
    Vec3 lookFrom;
    Vec3 lookAt;
    Vec3 up;
    double fovY;
    double aperture = 0;
    double focusDistance = 0;
};

inline CameraSetup demoCameraSetup()
{
    return {Vec3(0.15, 1.55, 5.5), Vec3(0.0, 0.7, 0.15), Vec3(0, 1, 0), 42.0};
}

inline Camera createDemoCamera(double aspect)
{
    CameraSetup setup = demoCameraSetup();
    return Camera(setup.lookFrom, setup.lookAt, setup.up, setup.fovY, aspect);
}
