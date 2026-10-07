#include "SelfTest.hpp"

#include "DemoScene.hpp"
#include "GpuLimits.hpp"
#include "MeshImport.hpp"
#include "Play.hpp"
#include "RayTracer.hpp"

#include <filesystem>
#include <iostream>

int runSelfTests()
{
    int failed = runPlaySelfTests();
    auto expect = [&](bool condition, const char *name) {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            ++failed;
        }
    };

    Scene scene = createDemoScene();
    const GpuSceneLimits limits = gpuSceneLimits(scene);
    expect(limits.spheres > 0 && limits.planes > 0, "GPU scene contribution sees demo shapes");

    RayTracer tracer;
    HitRecord hit;
    expect(scene.intersect(Ray(Vec3(0, 1, 6), normalize(Vec3(0, -0.1, -1))), 0.001, 1000, hit),
           "demo scene traces on CPU");

    const std::filesystem::path fbx = std::filesystem::temp_directory_path() / "raytracer-entt-self-test.fbx";
    if (mesh_import::writeTestTriangleFbx(fbx))
    {
        std::string error;
        Object *mesh = scene.addMesh();
        expect(mesh->loadMesh(fbx, error) && !mesh->triangles().empty(), "FBX imports into MeshShape");
        std::error_code ignored;
        std::filesystem::remove(fbx, ignored);
    }
    else
        expect(false, "FBX test file writes");

    if (failed == 0)
    {
        std::cout << "self-test: ok\n";
        return 0;
    }
    std::cerr << "self-test: " << failed << " failed\n";
    return 1;
}
