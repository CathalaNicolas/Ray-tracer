#include "SceneFile.hpp"

#include "SceneWrite.hpp"
#include "Terrain.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>

bool saveScene(const std::filesystem::path &path, const Scene &scene, const CameraSetup &camera, std::string &error)
{
    std::ofstream out(path);
    if (!out)
    {
        error = "Could not write " + path.string();
        return false;
    }

    out << std::setprecision(17);
    out << "raytracer-scene 2\n";
    out << "next_id " << scene.nextId() << '\n';
    out << "ambient " << scene.ambient().x << ' ' << scene.ambient().y << ' ' << scene.ambient().z << '\n';
    out << "background " << scene.horizon().x << ' ' << scene.horizon().y << ' ' << scene.horizon().z << ' '
        << scene.zenith().x << ' ' << scene.zenith().y << ' ' << scene.zenith().z << '\n';
    out << "camera " << camera.lookFrom.x << ' ' << camera.lookFrom.y << ' ' << camera.lookFrom.z << ' '
        << camera.lookAt.x << ' ' << camera.lookAt.y << ' ' << camera.lookAt.z << ' ' << camera.fovY;
    if (camera.aperture > 0 || camera.focusDistance > 0)
        out << ' ' << camera.aperture << ' ' << camera.focusDistance;
    out << '\n';
    out << "exposure " << scene.exposure() << '\n';
    if (scene.fogDensity() > 0)
        out << "fog " << scene.fogColor().x << ' ' << scene.fogColor().y << ' ' << scene.fogColor().z << ' ' << scene.fogDensity() << '\n';
    for (const SceneCamera &shot : scene.shots())
    {
        out << "shot \"" << scene_write::escapeName(shot.name) << "\" "
            << shot.lookFrom.x << ' ' << shot.lookFrom.y << ' ' << shot.lookFrom.z << ' '
            << shot.lookAt.x << ' ' << shot.lookAt.y << ' ' << shot.lookAt.z << ' '
            << shot.fov << '\n';
    }
    if (!scene.environment().empty())
        out << "environment \"" << scene_write::escapeName(scene.environment()) << "\"\n";
    out << "map " << scene.map().id << " \"" << scene_write::escapeName(scene.map().name) << "\" "
        << mapKindName(scene.map().kind) << ' ' << scene.map().tileSize << ' ' << scene.map().tilesX << ' '
        << scene.map().tilesZ << '\n';
    for (const LiquidVolume &volume : scene.liquids())
    {
        out << "liquid " << liquidKindName(volume.kind) << ' '
            << volume.boundsMin.x << ' ' << volume.boundsMin.y << ' ' << volume.boundsMin.z << ' '
            << volume.boundsMax.x << ' ' << volume.boundsMax.y << ' ' << volume.boundsMax.z << ' '
            << volume.surfaceY << '\n';
    }

    for (const auto &object : scene.objects())
        object->writeScene(out);

    for (const PointLight &light : scene.lights())
    {
        out << "light \"" << scene_write::escapeName(light.name) << "\" "
            << light.position.x << ' ' << light.position.y << ' ' << light.position.z << ' '
            << light.color.x << ' ' << light.color.y << ' ' << light.color.z << ' '
            << light.intensity << ' ' << light.falloff;
        if (light.radius != 0)
            out << " radius " << light.radius;
        if (light.directional)
            out << " directional";
        if (light.spotOuter > 0 || light.spotInner > 0
            || std::abs(light.spotDirection.x) + std::abs(light.spotDirection.y) + std::abs(light.spotDirection.z) > 1e-8)
        {
            out << " spot " << light.spotDirection.x << ' ' << light.spotDirection.y << ' ' << light.spotDirection.z
                << ' ' << light.spotOuter << ' ' << light.spotInner;
        }
        out << '\n';
    }

    error.clear();
    return true;
}
