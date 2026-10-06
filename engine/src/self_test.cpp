#include "SelfTest.hpp"

#include "Camera.hpp"
#include "DemoScene.hpp"
#include "EngineSettings.hpp"
#include "GpuLimits.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "RayTracer.hpp"
#include "SceneFile.hpp"
#include "SceneParse.hpp"
#include "Sound.hpp"
#include "Sphere.hpp"

#include "stb/stb_image_write.h"

#include <fbxsdk.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{

int g_failed = 0;

void expect(bool condition, const std::string &name)
{
    if (!condition)
    {
        std::cerr << "FAIL " << name << "\n";
        ++g_failed;
    }
}

bool near(double a, double b, double eps = 1e-5)
{
    return std::abs(a - b) <= eps;
}

bool near(const Vec3 &a, const Vec3 &b, double eps = 1e-5)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

Material flatWhite()
{
    Material material;
    material.albedo = Vec3(1, 1, 1);
    material.ambient = 0;
    material.diffuse = 1;
    material.specular = 0;
    material.shininess = 1;
    material.reflectivity = 0;
    return material;
}

void testVectors()
{
    Vec3 scaled = Vec3(2, -3, 4) * 3;
    expect(near(scaled, Vec3(6, -9, 12)), "vector * scalar multiplies");
    expect(near(3 * Vec3(2, -3, 4), scaled), "scalar * vector matches");
    expect(near(Vec3(5, 1, 0) - Vec3(1, 4, -2), Vec3(4, -3, 2)), "vector subtraction");
    expect(near(length(normalize(Vec3(3, 4, 0))), 1), "normalize produces a unit vector");
    expect(near(cross(Vec3(1, 0, 0), Vec3(0, 1, 0)), Vec3(0, 0, 1)), "cross product is right-handed");
    expect(near(reflect(Vec3(0, -1, 0), Vec3(0, 1, 0)), Vec3(0, 1, 0)), "reflection off an upward normal");
}

void testIntersections()
{
    Sphere sphere(Vec3(0, 0, 0), 1, flatWhite());
    HitRecord hit;
    Ray toward(Vec3(0, 0, -5), Vec3(0, 0, 1));
    expect(sphere.intersect(toward, 0, 100, hit), "ray hits the sphere");
    expect(near(hit.t, 4), "nearest sphere intersection is t = 4");
    expect(near(hit.normal, Vec3(0, 0, -1)), "sphere normal faces the incoming ray");

    Ray miss(Vec3(0, 0, -5), Vec3(1, 0, 0));
    expect(!sphere.intersect(miss, 0, 100, hit), "ray missing the sphere");

    Ray behind(Vec3(0, 0, 5), Vec3(0, 0, 1));
    expect(!sphere.intersect(behind, 0, 100, hit), "sphere behind the ray is ignored");

    Plane plane(Vec3(0, 0, 0), Vec3(0, 5, 0), flatWhite());
    Ray down(Vec3(0, 4, 0), Vec3(0, -1, 0));
    expect(plane.intersect(down, 0, 100, hit), "ray hits the plane");
    expect(near(hit.t, 4), "plane intersection distance");
    expect(near(hit.normal, Vec3(0, 1, 0)), "non-unit plane normal is normalized");

    Ray up(Vec3(0, 4, 0), Vec3(0, 1, 0));
    expect(!plane.intersect(up, 0, 100, hit), "ray leaving the plane does not hit it");

    Plane checks(Vec3(0, 0, 0), Vec3(0, 1, 0), flatWhite());
    checks.setChecker(Vec3(0, 0, 0), 1);
    expect(checks.intersect(Ray(Vec3(0.5, 2, 0.5), Vec3(0, -1, 0)), 0, 100, hit), "checker tile is hit");
    expect(near(hit.material.albedo, Vec3(1, 1, 1)), "even checker tile keeps the base albedo");
    expect(checks.intersect(Ray(Vec3(1.5, 2, 0.5), Vec3(0, -1, 0)), 0, 100, hit), "odd checker tile is hit");
    expect(near(hit.material.albedo, Vec3(0, 0, 0)), "odd checker tile uses the second albedo");
}

void testClosestAndRange()
{
    Scene scene;
    scene.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, flatWhite()));
    scene.add(std::make_unique<Sphere>(Vec3(0, 0, -3), 0.5, flatWhite()));

    HitRecord hit;
    expect(scene.intersect(Ray(Vec3(0, 0, -8), Vec3(0, 0, 1)), 0, 100, hit), "scene finds an object");
    expect(near(hit.t, 4.5), "closer sphere wins");

    Scene far;
    far.add(std::make_unique<Sphere>(Vec3(0, 0, 5), 1, flatWhite()));
    expect(!far.intersect(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), 0, 3, hit), "objects beyond tMax are ignored");
    expect(far.intersect(Ray(Vec3(0, 0, 0), Vec3(0, 0, 1)), 0, 10, hit), "sphere inside tMax is found");
}

void testShading()
{
    RayTracer tracer;
    tracer.maxDepth = 0;

    Scene lit;
    lit.setAmbient(Vec3(0, 0, 0));
    lit.setBackground(Vec3(0, 1, 0), Vec3(0, 1, 0));
    lit.add(std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 5, 0), flatWhite()));
    lit.add(std::make_unique<Sphere>(Vec3(0, 6, 0), 0.5, flatWhite()));
    lit.addLight(PointLight(Vec3(0, 2, 0), Vec3(1, 1, 1), 1, 0));

    Vec3 color = tracer.trace(Ray(Vec3(0, 3, 0), Vec3(0, -1, 0)), lit, 0);
    expect(near(color, Vec3(1, 1, 1), 1e-4), "unit Lambertian response with a normalized normal");

    Scene clear;
    clear.setAmbient(Vec3(0, 0, 0));
    clear.setBackground(Vec3(0, 1, 0), Vec3(0, 1, 0));
    clear.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, flatWhite()));
    clear.addLight(PointLight(Vec3(0, 0, -5), Vec3(1, 1, 1), 1, 0));
    color = tracer.trace(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), clear, 0);
    expect(near(color, Vec3(1, 1, 1), 1e-4), "a sphere does not shadow itself");

    Scene blocked;
    blocked.setAmbient(Vec3(0, 0, 0));
    blocked.setBackground(Vec3(0, 1, 0), Vec3(0, 1, 0));
    blocked.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, flatWhite()));
    blocked.add(std::make_unique<Sphere>(Vec3(0, 0.9, -1.6), 0.25, flatWhite()));
    blocked.addLight(PointLight(Vec3(0, 2, -3), Vec3(1, 1, 1), 1, 0));
    color = tracer.trace(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), blocked, 0);
    expect(near(color, Vec3(0, 0, 0), 1e-4), "a blocker between the surface and the light casts a shadow");
}

void testCamera()
{
    Camera camera(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0), 90, 1);
    Ray center = camera.getRay(0.5, 0.5);
    expect(near(center.direction, Vec3(0, 0, -1)), "center ray looks at the target");

    Ray right = camera.getRay(0.85, 0.5);
    expect(right.direction.x > 0.2, "a ray on the right of the frame points right");
    Ray upper = camera.getRay(0.5, 0.85);
    expect(upper.direction.y > 0.2, "a ray on the top of the frame points up");

    Vec3 from(0, 0, 5);
    Vec3 at(0, 0, 0);
    orbitCamera(from, at, 0.4, 0.2);
    expect(near(length(from - at), 5, 1e-4), "orbit keeps the camera distance");

    Vec3 parkedFrom(0, 1, 6);
    Vec3 parkedAt(0, 1, 0);
    Vec3 offset = parkedFrom - parkedAt;
    panCamera(parkedFrom, parkedAt, 30, -10);
    expect(near(parkedFrom - parkedAt, offset), "pan keeps the view direction");
    expect(length(parkedAt - Vec3(0, 1, 0)) > 0.01, "pan moves the target");

    Vec3 dollyFrom(0, 0, 4);
    dollyCamera(dollyFrom, Vec3(0, 0, 0), 0.5);
    expect(near(length(dollyFrom), 2, 1e-4), "dolly moves the camera closer");
}

void testParseTail()
{
    scene_parse::SurfaceExtras extras;
    std::string error;
    expect(scene_parse::parseTail("layer 1 prefab \"Gem\"", extras, error), "a tail with layer and prefab parses");
    expect(error.empty() && extras.layer == 1 && extras.prefab == "Gem", "layer 1 and prefab Gem are read from the tail");
}

void testSceneFile()
{
    Scene scene = createDemoScene();
    auto extra = std::make_unique<Sphere>(Vec3(2, 1, 0), 0.3, Material::makeDiffuse(Vec3(0.1, 0.2, 0.3)));
    extra->name = "Say \"hi\"";
    extra->tag = "player";
    extra->motion.move = Vec3(1, 2, 3);
    extra->motion.rotate = Vec3(4, 5, 6);
    extra->motion.scale = 0.5;
    extra->motion.period = 2.5;
    extra->spawnEvery = 1.25;
    extra->action.target = "Door";
    extra->action.move = Vec3(0, 1.5, 0);
    const int extraParent = scene.objects().front()->id;
    extra->parentId = extraParent;
    Material scrolled = extra->material();
    scrolled.uvScrollU = 0.25;
    scrolled.uvScrollV = -0.5;
    extra->setMaterial(scrolled);
    extra->prefab = "Gem";
    scene.add(std::move(extra));
    scene.setFog(Vec3(0.2, 0.3, 0.4), 0.15);
    SceneCamera shot;
    shot.name = "Wide";
    shot.lookFrom = Vec3(1, 2, 3);
    shot.lookAt = Vec3(0, 1, 0);
    shot.fov = 55;
    scene.shots().push_back(shot);
    CameraSetup camera = demoCameraSetup();
    camera.lookFrom = Vec3(1, 2, 3);
    camera.aperture = 0.2;
    camera.focusDistance = 3.5;
    const std::filesystem::path path = "self-test-scene.scene";
    std::string error;
    expect(saveScene(path, scene, camera, error), "scene file can be written");

    Scene loaded;
    CameraSetup loadedCamera;
    expect(loadScene(path, loaded, loadedCamera, error), "scene file can be read");
    std::filesystem::remove(path);
    expect(near(loaded.ambient(), scene.ambient()), "ambient light is restored");
    expect(near(loadedCamera.lookFrom, camera.lookFrom), "camera position is restored");
    expect(near(loadedCamera.fovY, camera.fovY), "field of view is restored");
    expect(near(loadedCamera.aperture, 0.2) && near(loadedCamera.focusDistance, 3.5), "lens settings are restored");
    expect(loaded.objects().size() == scene.objects().size(), "every object is restored");
    expect(loaded.lights().size() == 2 && loaded.lights()[0].name == "Key", "light names are restored");

    const Plane *ground = nullptr;
    const Sphere *quoted = nullptr;
    const Sphere *gold = nullptr;
    const Sphere *lamp = nullptr;
    const Mesh *trunk = nullptr;
    for (const auto &object : loaded.objects())
    {
        if (object->name == "Ground")
            ground = dynamic_cast<const Plane *>(object.get());
        if (object->name == "Say \"hi\"")
            quoted = dynamic_cast<const Sphere *>(object.get());
        if (object->name == "Gold sphere")
            gold = dynamic_cast<const Sphere *>(object.get());
        if (object->name == "Lamp")
            lamp = dynamic_cast<const Sphere *>(object.get());
        if (object->name == "Tree trunk")
            trunk = dynamic_cast<const Mesh *>(object.get());
    }
    expect(ground != nullptr && ground->checker() && near(ground->checkerScale(), 1.15), "checker plane is restored");
    expect(quoted != nullptr && near(quoted->radius(), 0.3), "quoted object names round-trip");
    expect(quoted != nullptr && quoted->tag == "player", "a sphere tag round-trips");
    expect(quoted != nullptr && near(quoted->motion.move, Vec3(1, 2, 3)) && near(quoted->motion.rotate, Vec3(4, 5, 6)) && near(quoted->motion.scale, 0.5) && near(quoted->motion.period, 2.5), "motion round-trips");
    expect(quoted != nullptr && near(quoted->spawnEvery, 1.25), "a spawn interval round-trips");
    expect(quoted != nullptr && quoted->action.target == "Door" && near(quoted->action.move, Vec3(0, 1.5, 0)), "a use action round-trips");
    expect(quoted != nullptr && quoted->parentId == extraParent && near(quoted->localCenter(), Vec3(2, 1, 0)), "a parent id round-trips");
    expect(quoted != nullptr && near(quoted->material().uvScrollU, 0.25) && near(quoted->material().uvScrollV, -0.5), "uv scroll round-trips");
    expect(quoted != nullptr && quoted->prefab == "Gem", "a prefab name round-trips");
    Hittable *gem = loaded.find(quoted->id);
    const int placed = gem == nullptr ? -1 : placePrefabInstance(loaded, gem->id);
    expect(placed > 0, "a prefab can be placed");
    if (auto *sphere = dynamic_cast<Sphere *>(gem))
        sphere->setRadius(0.44);
    syncPrefabInstances(loaded);
    const Sphere *instance = dynamic_cast<const Sphere *>(loaded.find(placed));
    expect(instance != nullptr && instance->instanceOf == "Gem" && near(instance->radius(), 0.44), "editing a prefab updates its placement");
    expect(gold != nullptr && gold->parentId == 0, "a parent id of 0 is omitted");
    expect(near(loaded.fogColor(), Vec3(0.2, 0.3, 0.4)) && near(loaded.fogDensity(), 0.15), "fog round-trips");
    expect(loaded.shots().size() == 1 && loaded.shots()[0].name == "Wide" && near(loaded.shots()[0].lookFrom, Vec3(1, 2, 3)) && near(loaded.shots()[0].fov, 55), "a named camera round-trips");
    expect(ground != nullptr && ground->tag == "solid", "a plane tag round-trips");
    expect(gold != nullptr && gold->tag.empty(), "objects without a tag stay untagged");
    expect(gold != nullptr && near(gold->material().roughness, 0.34), "roughness is restored");
    expect(lamp != nullptr && near(lamp->material().emission, 8), "emission is restored");
    expect(trunk != nullptr && near(trunk->rotation().y, 18), "mesh rotation is restored");
    expect(trunk != nullptr && trunk->tag == "solid", "a mesh tag round-trips");
    if (!error.empty() && g_failed > 0)
        std::cerr << error << "\n";
}

void testParentTransform()
{
    Scene scene;
    const int parentId = scene.add(std::make_unique<Sphere>(Vec3(1, 2, 3), 0.5, flatWhite()));
    auto child = std::make_unique<Sphere>(Vec3(0.5, 0, -1), 0.2, flatWhite());
    child->parentId = parentId;
    const int childId = scene.add(std::move(child));
    auto *parent = dynamic_cast<Sphere *>(scene.find(parentId));
    auto *sphere = dynamic_cast<Sphere *>(scene.find(childId));
    expect(parent != nullptr && sphere != nullptr && near(sphere->center(), parent->center() + sphere->localCenter()), "a child world center is the parent world plus its local center");
    parent->setCenter(Vec3(4, 1, -2));
    expect(near(sphere->center(), Vec3(4.5, 1, -3)), "moving the parent moves the child world center");
    expect(near(sphere->localCenter(), Vec3(0.5, 0, -1)), "moving the parent leaves the child local center");
    sphere->parentId = childId;
    sphere->notifyTransformChanged();
    expect(near(sphere->center(), sphere->localCenter()), "a parent of itself is ignored");
    parent->parentId = childId;
    sphere->parentId = parentId;
    scene.bumpParentFrames();
    expect(near(sphere->center(), parent->localCenter() + sphere->localCenter()), "a cycle adds each local position once");

    Scene turned;
    auto mesh = std::make_unique<Mesh>();
    mesh->setPosition(Vec3(2, 0, 0));
    mesh->setRotation(Vec3(0, 90, 0));
    mesh->setScale(2);
    const int meshId = turned.add(std::move(mesh));
    auto orb = std::make_unique<Sphere>(Vec3(1, 0, 0), 0.3, flatWhite());
    orb->parentId = meshId;
    const int orbId = turned.add(std::move(orb));
    auto *placed = dynamic_cast<Sphere *>(turned.find(orbId));
    auto *owner = dynamic_cast<Mesh *>(turned.find(meshId));
    expect(placed != nullptr && near(placed->center(), Vec3(2, 0, -2)), "a mesh parent rotates and scales the child offset");
    expect(placed != nullptr && near(placed->worldRadius(), 0.6), "a mesh parent scales the child radius");
    expect(owner != nullptr && near(owner->worldScale(), 2), "a mesh with no parent keeps its own scale");
    expect(placed != nullptr && near(placed->localCenter(), Vec3(1, 0, 0)), "parent rotation leaves the child local center");
}

void testGpuLimits()
{
    Scene scene;
    for (int i = 0; i < kGpuMaxSpheres + 1; ++i)
        scene.add(std::make_unique<Sphere>(Vec3(i, 0, 0), 0.2, flatWhite()));
    for (int i = 0; i < kGpuMaxPlanes + 1; ++i)
        scene.add(std::make_unique<Plane>(Vec3(0, i, 0), Vec3(0, 1, 0), flatWhite()));
    for (int i = 0; i < kGpuMaxLights + 1; ++i)
        scene.addLight(PointLight(Vec3(0, i, 0), Vec3(1, 1, 1), 1, 0));

    GpuSceneLimits limits = gpuSceneLimits(scene);
    expect(limits.spheres == kGpuMaxSpheres + 1, "sphere count includes every sphere");
    expect(limits.planes == kGpuMaxPlanes + 1, "plane count includes every plane");
    expect(limits.lights == kGpuMaxLights + 1, "light count includes every light");
    const std::string warning = limits.warning();
    expect(warning.find("64/65 spheres") != std::string::npos, "the GPU warning names dropped spheres");
    expect(warning.find("16/17 planes") != std::string::npos, "the GPU warning names dropped planes");
    expect(warning.find("8/9 lights") != std::string::npos, "the GPU warning names dropped lights");
    expect(gpuSceneLimits(createDemoScene()).warning().empty(), "the demo scene fits on the GPU");
}

void testEditing()
{
    Scene scene;
    int id = scene.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, flatWhite()));
    expect(id > 0, "scene assigns an id");
    Hittable *object = scene.find(id);
    expect(object != nullptr, "find returns the added sphere");
    auto *sphere = dynamic_cast<Sphere *>(object);
    expect(sphere != nullptr && near(sphere->radius(), 1), "settings read the sphere radius");

    Scene copy = scene.clone();
    sphere->setRadius(0.25);
    sphere->setCenter(Vec3(4, 0, 0));
    auto *copied = dynamic_cast<Sphere *>(copy.find(id));
    expect(copied != nullptr && near(copied->radius(), 1), "a cloned scene keeps its own settings");

    HitRecord hit;
    expect(scene.intersect(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), 0, 100, hit) == false, "moved sphere is no longer hit");
    expect(copy.intersect(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), 0, 100, hit), "clone is still hit");
    expect(hit.objectId == id, "hit records the selected object id");

    expect(scene.remove(id), "delete removes the sphere");
    expect(scene.find(id) == nullptr, "deleted object cannot be selected");
    expect(!scene.remove(id), "deleting an object twice does nothing");
}

void testDemoFrame()
{
    const int width = 120;
    const int height = 90;
    Image image(width, height);
    RayTracer tracer;
    tracer.sampleGrid = 1;
    tracer.maxDepth = 3;
    tracer.render(createDemoScene(), createDemoCamera(static_cast<double>(width) / height), image);

    int redPixels = 0;
    int skyPixels = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            Vec3 pixel = image.at(x, y);
            if (pixel.x > pixel.y + 0.15 && pixel.x > pixel.z + 0.15 && pixel.x > 0.2)
                ++redPixels;
            if (pixel.z > pixel.x + 0.05 && pixel.z > 0.4)
                ++skyPixels;
        }
    }

    expect(redPixels > 20, "demo frame shows the red sphere");
    expect(skyPixels > 20, "demo frame shows the sky");
}

void testRenderingFeatures()
{
    RayTracer tracer;
    tracer.maxDepth = 0;

    Scene glass;
    glass.setAmbient(Vec3(0, 0, 0));
    glass.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    Material red = flatWhite();
    red.albedo = Vec3(1, 0, 0);
    glass.add(std::make_unique<Plane>(Vec3(0, 0, 2), Vec3(0, 0, -1), red));
    glass.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, Material::makeGlass(Vec3(1, 1, 1), 1.5)));
    glass.addLight(PointLight(Vec3(0, 0.5, -1), Vec3(1, 1, 1), 1, 0));
    Vec3 color = tracer.trace(Ray(Vec3(0, 0, -4), Vec3(0, 0, 1)), glass, 4);
    expect(color.x > color.y && color.x > color.z && color.x > 0.3, "glass transmits the red plane");

    Scene sunlit;
    sunlit.setAmbient(Vec3(0, 0, 0));
    sunlit.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    sunlit.add(std::make_unique<Plane>(Vec3(0, 0, 0), Vec3(0, 0, 1), flatWhite()));
    PointLight sun(Vec3(0, 0, 1), Vec3(1, 1, 1), 1, 0);
    sun.directional = true;
    sunlit.addLight(sun);
    color = tracer.trace(Ray(Vec3(0, 0, 4), Vec3(0, 0, -1)), sunlit, 0);
    expect(near(color, Vec3(1, 1, 1), 1e-3), "a directional light lights a facing plane");
    sunlit.lights()[0].position = Vec3(0, 0, -1);
    color = tracer.trace(Ray(Vec3(0, 0, 4), Vec3(0, 0, -1)), sunlit, 0);
    expect(near(color, Vec3(0, 0, 0), 1e-3), "a directional light from behind leaves the plane dark");

    MeshTri triangle;
    triangle.position[0] = Vec3(-1, -1, 0);
    triangle.position[1] = Vec3(1, -1, 0);
    triangle.position[2] = Vec3(0, 1, 0);
    triangle.normal[0] = triangle.normal[1] = triangle.normal[2] = Vec3(0, 0, -1);
    auto mesh = std::make_unique<Mesh>();
    Material green = flatWhite();
    green.albedo = Vec3(0, 1, 0);
    mesh->setMaterial(green);
    mesh->setTriangles({triangle});
    HitRecord hit;
    expect(mesh->intersect(Ray(Vec3(0, 0, -3), Vec3(0, 0, 1)), 0, 100, hit), "a triangle is hit");
    Scene meshed;
    meshed.setAmbient(Vec3(0, 0, 0));
    meshed.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    meshed.add(std::move(mesh));
    meshed.addLight(PointLight(Vec3(0, 0, -2), Vec3(1, 1, 1), 1, 0));
    color = tracer.trace(Ray(Vec3(0, 0, -3), Vec3(0, 0, 1)), meshed, 0);
    expect(color.y > color.x && color.y > 0.4, "a triangle mesh is shaded");

    const std::filesystem::path objPath = "self-test-mesh.obj";
    {
        std::ofstream out(objPath);
        out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    Mesh loadedMesh;
    std::string error;
    expect(loadedMesh.load(objPath, error), "an OBJ file loads");
    Mesh secondMesh;
    expect(secondMesh.load(objPath, error), "a second OBJ load reuses the file");
    expect(secondMesh.geometry() == loadedMesh.geometry(), "two loads share one mesh");
    secondMesh.setPosition(Vec3(3, 0, 0));
    expect(near(loadedMesh.position().x, 0), "a shared mesh keeps its own position");
    auto cloned = loadedMesh.clone();
    const Mesh *copied = dynamic_cast<const Mesh *>(cloned.get());
    expect(copied != nullptr && copied->geometry() == loadedMesh.geometry(), "a mesh copy shares its triangles");
    Scene copies;
    copies.add(std::move(cloned));
    copies.add(secondMesh.clone());
    copies.add(loadedMesh.clone());
    const GpuSceneLimits sharedLimits = gpuSceneLimits(copies);
    expect(sharedLimits.meshes == 3 && sharedLimits.triangles == static_cast<int>(loadedMesh.triangles().size()), "shared placements count their triangles once");
    std::filesystem::remove(objPath);
    expect(loadedMesh.triangles().size() == 1, "an OBJ file produces a triangle");

    const std::filesystem::path fbxPath = "self-test-mesh.fbx";
    {
        FbxManager *manager = FbxManager::Create();
        FbxIOSettings *settings = FbxIOSettings::Create(manager, IOSROOT);
        manager->SetIOSettings(settings);
        FbxScene *scene = FbxScene::Create(manager, "test");
        FbxMesh *shape = FbxMesh::Create(scene, "Triangle");
        shape->InitControlPoints(3);
        shape->SetControlPointAt(FbxVector4(0, 0, 0), 0);
        shape->SetControlPointAt(FbxVector4(1, 0, 0), 1);
        shape->SetControlPointAt(FbxVector4(0, 1, 0), 2);
        shape->BeginPolygon();
        shape->AddPolygon(0);
        shape->AddPolygon(1);
        shape->AddPolygon(2);
        shape->EndPolygon();
        FbxNode *node = FbxNode::Create(scene, "Triangle");
        node->SetNodeAttribute(shape);
        scene->GetRootNode()->AddChild(node);
        FbxExporter *exporter = FbxExporter::Create(manager, "");
        const bool opened = exporter->Initialize(fbxPath.string().c_str(), -1, manager->GetIOSettings());
        const bool exported = opened && exporter->Export(scene);
        exporter->Destroy();
        manager->Destroy();
        expect(exported, "an FBX file can be written");
    }
    Mesh fbxMesh;
    expect(fbxMesh.load(fbxPath, error), "an FBX file loads");
    std::filesystem::remove(fbxPath);
    expect(fbxMesh.triangles().size() == 1, "an FBX file produces a triangle");
    if (!loadedMesh.triangles().empty())
    {
        const MeshTri &loaded = loadedMesh.triangles()[0];
        const Vec3 center = (loaded.position[0] + loaded.position[1] + loaded.position[2]) / 3.0;
        const bool fromFront = loadedMesh.intersect(Ray(center + Vec3(0, 0, 2), Vec3(0, 0, -1)), 0, 100, hit);
        const bool fromBack = loadedMesh.intersect(Ray(center + Vec3(0, 0, -2), Vec3(0, 0, 1)), 0, 100, hit);
        expect(fromFront || fromBack, "a loaded OBJ is hittable");
    }

    const std::filesystem::path albedoPath = "self-test-albedo.png";
    unsigned char redPixels[16];
    for (int pixel = 0; pixel < 4; ++pixel)
    {
        redPixels[pixel * 4] = 255;
        redPixels[pixel * 4 + 1] = 0;
        redPixels[pixel * 4 + 2] = 0;
        redPixels[pixel * 4 + 3] = 255;
    }
    expect(stbi_write_png(albedoPath.string().c_str(), 2, 2, 4, redPixels, 8) != 0, "an albedo image can be written");
    Scene textured;
    textured.setAmbient(Vec3(0, 0, 0));
    textured.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    Material mapped = flatWhite();
    mapped.albedo = Vec3(0, 0, 1);
    mapped.albedoMap = albedoPath.u8string();
    textured.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, mapped));
    textured.addLight(PointLight(Vec3(0, 0, -5), Vec3(1, 1, 1), 1, 0));
    color = tracer.trace(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), textured, 0);
    std::filesystem::remove(albedoPath);
    expect(color.x > 0.5 && color.x > color.z, "an albedo image replaces the flat color");

    const std::filesystem::path envPath = "self-test-env.png";
    unsigned char bluePixels[16];
    for (int pixel = 0; pixel < 4; ++pixel)
    {
        bluePixels[pixel * 4] = 0;
        bluePixels[pixel * 4 + 1] = 0;
        bluePixels[pixel * 4 + 2] = 255;
        bluePixels[pixel * 4 + 3] = 255;
    }
    expect(stbi_write_png(envPath.string().c_str(), 2, 2, 4, bluePixels, 8) != 0, "an environment image can be written");
    Scene sky;
    sky.setAmbient(Vec3(0, 0, 0));
    sky.setEnvironment(envPath.u8string());
    sky.setExposure(1);
    color = tracer.trace(Ray(Vec3(0, 0, 0), Vec3(0, 1, 0)), sky, 0);
    std::filesystem::remove(envPath);
    expect(color.z > 0.5 && color.z > color.x, "an environment map replaces the sky gradient");

    Scene stored;
    auto glassBall = std::make_unique<Sphere>(Vec3(0, 1, 0), 0.4, Material::makeGlass(Vec3(0.9, 0.95, 1), 1.45));
    glassBall->name = "Glass";
    stored.add(std::move(glassBall));
    PointLight sunLight(Vec3(0, 1, 0), Vec3(1, 1, 1), 1.2, 0);
    sunLight.name = "Sun";
    sunLight.directional = true;
    sunLight.radius = 4;
    stored.addLight(sunLight);
    const std::filesystem::path scenePath = "self-test-features.scene";
    CameraSetup camera;
    expect(saveScene(scenePath, stored, camera, error), "glass and directional lights can be saved");
    Scene restored;
    CameraSetup restoredCamera;
    expect(loadScene(scenePath, restored, restoredCamera, error), "glass and directional lights can be loaded");
    std::filesystem::remove(scenePath);
    const bool lightRestored = restored.lights().size() == 1 && restored.lights()[0].directional && near(restored.lights()[0].radius, 4);
    expect(lightRestored, "directional softness is restored");
    const Sphere *roundTrip = restored.objects().empty() ? nullptr : dynamic_cast<const Sphere *>(restored.objects()[0].get());
    expect(roundTrip != nullptr && near(roundTrip->material().transmission, 1) && near(roundTrip->material().ior, 1.45), "glass settings are restored");
    if (!error.empty() && g_failed > 0)
        std::cerr << error << "\n";
}

void testMeshRotation()
{
    MeshTri triangle;
    triangle.position[0] = Vec3(0, 0, 0);
    triangle.position[1] = Vec3(1, 0, 0);
    triangle.position[2] = Vec3(0, 1, 0);
    triangle.normal[0] = triangle.normal[1] = triangle.normal[2] = Vec3(0, 0, 1);
    Mesh mesh;
    mesh.setTriangles({triangle});
    HitRecord hit;
    expect(mesh.intersect(Ray(Vec3(0.2, 0.2, -1), Vec3(0, 0, 1)), 0, 10, hit), "an unrotated triangle is hit face-on");
    mesh.setRotation(Vec3(0, 90, 0));
    expect(!mesh.intersect(Ray(Vec3(0.2, 0.2, -1), Vec3(0, 0, 1)), 0, 10, hit), "yaw swings the triangle out of the ray");
    expect(mesh.intersect(Ray(Vec3(-1, 0.2, -0.2), Vec3(1, 0, 0)), 0, 10, hit), "yaw presents the triangle to a side ray");
    expect(near(hit.normal, Vec3(-1, 0, 0), 1e-4), "a rotated normal faces the incoming ray");
}

void testMaterialFeatures()
{
    RayTracer tracer;
    tracer.maxDepth = 0;
    Scene scene;
    scene.setAmbient(Vec3(0, 0, 0));
    scene.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    Material lamp = flatWhite();
    lamp.ambient = 0;
    lamp.diffuse = 0;
    lamp.emission = 2;
    scene.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, lamp));
    Vec3 linear = tracer.trace(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), scene, 0);
    expect(near(linear, Vec3(2, 2, 2), 1e-3), "linear output keeps emission unclamped");

    Image image(1, 1);
    tracer.sampleGrid = 1;
    tracer.render(scene, Camera(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3(0, 1, 0), 40, 1), image);
    Vec3 shown = image.at(0, 0);
    expect(shown.x > 0.5 && shown.x < 0.85, "the display path rolls a bright surface off white");

    const std::filesystem::path normalPath = "self-test-normal.png";
    unsigned char tilted[4] = {128, 255, 128, 255};
    expect(stbi_write_png(normalPath.string().c_str(), 1, 1, 4, tilted, 4) != 0, "a normal map can be written");
    Scene mapped;
    mapped.setAmbient(Vec3(0, 0, 0));
    mapped.setBackground(Vec3(0, 0, 0), Vec3(0, 0, 0));
    Material surface = flatWhite();
    surface.normalMap = normalPath.u8string();
    mapped.add(std::make_unique<Sphere>(Vec3(0, 0, 0), 1, surface));
    mapped.addLight(PointLight(Vec3(0, 0, -5), Vec3(1, 1, 1), 1, 0));
    Vec3 tiltedColor = tracer.trace(Ray(Vec3(0, 0, -5), Vec3(0, 0, 1)), mapped, 0);
    std::filesystem::remove(normalPath);
    expect(tiltedColor.x < 0.25, "a normal map tilts the shaded surface");
}

void testSoundFadeAndSettings()
{
    const double savedFar = engineSettings().soundFar;
    engineSettings().soundFar = 16;
    expect(near(soundDistanceFade(0), 1), "sound fade is full at the listener");
    expect(near(soundDistanceFade(8), 0.5), "sound fade is half at mid range");
    expect(near(soundDistanceFade(16), 0), "sound fade reaches zero at soundFar");
    expect(near(soundDistanceFade(32), 0), "sound fade stays zero past soundFar");
    engineSettings().soundFar = savedFar;

    expect(applyEngineSetting("snap", "0.25"), "snap setting parses");
    expect(near(engineSettings().snap, 0.25), "snap setting applies");
    expect(applyEngineSetting("mirrors", "0"), "mirrors setting parses");
    expect(!engineSettings().mirrorBounces, "mirrors 0 turns bounce off");
    expect(applyEngineSetting("mirrors", "1"), "mirrors setting can turn bounce on");
    expect(engineSettings().mirrorBounces, "mirrors 1 turns bounce on");
    expect(applyEngineSetting("bloom_threshold", "0.88"), "bloom threshold parses");
    expect(near(engineSettings().bloomThreshold, 0.88), "bloom threshold applies");
    expect(!applyEngineSetting("not_a_key", "1"), "unknown settings keys are ignored");
    engineSettings().snap = 0.5;
}

} // namespace

int runPlaySelfTests();

int runSelfTests()
{
    testVectors();
    testIntersections();
    testClosestAndRange();
    testShading();
    testCamera();
    testParseTail();
    testEditing();
    testSceneFile();
    testParentTransform();
    testGpuLimits();
    testDemoFrame();
    testRenderingFeatures();
    testMeshRotation();
    testMaterialFeatures();
    testSoundFadeAndSettings();
    g_failed += runPlaySelfTests();

    if (g_failed == 0)
    {
        std::cout << "self-test: ok\n";
        return 0;
    }

    std::cerr << "self-test: " << g_failed << " failed\n";
    return 1;
}
