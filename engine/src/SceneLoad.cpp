#include "SceneFile.hpp"
#include "SceneParse.hpp"

#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace scene_parse
{

bool readWord(const std::string &text, size_t &index, std::string &word)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
        ++index;
    if (index >= text.size() || text[index] == '"')
        return false;
    word.clear();
    while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index])))
        word.push_back(text[index++]);
    return !word.empty();
}

bool looksLikeNumber(const std::string &text, size_t index)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
        ++index;
    if (index >= text.size())
        return false;
    const unsigned char c = static_cast<unsigned char>(text[index]);
    if (std::isdigit(c) || c == '+' || c == '-' || c == '.')
        return true;
    return false;
}

bool readDoubleAt(const std::string &text, size_t &index, double &value)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
        ++index;
    if (index >= text.size() || !looksLikeNumber(text, index))
        return false;
    char *end = nullptr;
    value = std::strtod(text.c_str() + index, &end);
    if (end == text.c_str() + index)
        return false;
    index = static_cast<size_t>(end - text.c_str());
    return true;
}

bool parseTail(const std::string &tail, SurfaceExtras &extras, std::string &error, bool lights)
{
    size_t index = 0;
    while (index < tail.size())
    {
        while (index < tail.size() && std::isspace(static_cast<unsigned char>(tail[index])))
            ++index;
        if (index >= tail.size())
            break;
        std::string word;
        if (!readWord(tail, index, word))
        {
            error = "expected a keyword";
            return false;
        }
        if (word == "trans")
        {
            if (!readDoubleAt(tail, index, extras.transmission) || !readDoubleAt(tail, index, extras.ior))
            {
                error = "trans needs a transmission and an index of refraction";
                return false;
            }
        }
        else if (word == "uv")
        {
            if (!readDoubleAt(tail, index, extras.uvScale))
            {
                error = "uv needs a scale";
                return false;
            }
        }
        else if (word == "scroll")
        {
            if (!readDoubleAt(tail, index, extras.uvScrollU) || !readDoubleAt(tail, index, extras.uvScrollV))
            {
                error = "scroll needs a horizontal and a vertical speed";
                return false;
            }
        }
        else if (word == "texture")
        {
            if (!scene_parse::readQuoted(tail, index, extras.texture))
            {
                error = "texture needs a quoted path";
                return false;
            }
        }
        else if (word == "obj" || word == "fbx")
        {
            if (!scene_parse::readQuoted(tail, index, extras.objectFile))
            {
                error = word + " needs a quoted path";
                return false;
            }
        }
        else if (word == "checker")
        {
            double red = 0, green = 0, blue = 0, scale = 0;
            if (!readDoubleAt(tail, index, red) || !readDoubleAt(tail, index, green) || !readDoubleAt(tail, index, blue) || !readDoubleAt(tail, index, scale))
            {
                error = "checker needs a color and a scale";
                return false;
            }
            extras.checker = true;
            extras.checkerAlbedo = Vec3(red, green, blue);
            extras.checkerScale = scale;
        }
        else if (word == "radius")
        {
            if (!readDoubleAt(tail, index, extras.radius))
            {
                error = "radius needs a number";
                return false;
            }
        }
        else if (word == "directional")
        {
            extras.directional = true;
        }
        else if (lights && word == "spot")
        {
            double x = 0, y = 0, z = 0, outer = 0, inner = 0;
            if (!readDoubleAt(tail, index, x) || !readDoubleAt(tail, index, y) || !readDoubleAt(tail, index, z) || !readDoubleAt(tail, index, outer) || !readDoubleAt(tail, index, inner))
            {
                error = "spot needs a direction and two angles";
                return false;
            }
            extras.spotDirection = Vec3(x, y, z);
            extras.spotOuter = outer;
            extras.spotInner = inner;
        }
        else if (word == "rough")
        {
            if (!readDoubleAt(tail, index, extras.roughness))
            {
                error = "rough needs a number";
                return false;
            }
            extras.hasRoughness = true;
        }
        else if (word == "emit")
        {
            if (!readDoubleAt(tail, index, extras.emission))
            {
                error = "emit needs a number";
                return false;
            }
        }
        else if (word == "normal")
        {
            if (!scene_parse::readQuoted(tail, index, extras.normalMap))
            {
                error = "normal needs a quoted path";
                return false;
            }
        }
        else if (word == "rot")
        {
            double x = 0, y = 0, z = 0;
            if (!readDoubleAt(tail, index, x) || !readDoubleAt(tail, index, y) || !readDoubleAt(tail, index, z))
            {
                error = "rot needs three angles";
                return false;
            }
            extras.rotation = Vec3(x, y, z);
        }
        else if (word == "tag")
        {
            if (!scene_parse::readQuoted(tail, index, extras.tag))
            {
                error = "tag needs a quoted string";
                return false;
            }
        }
        else if (word == "motion")
        {
            double values[8] = {};
            for (double &value : values)
            {
                if (!readDoubleAt(tail, index, value))
                {
                    error = "motion needs a move, a rotation, a scale, and a period";
                    return false;
                }
            }
            extras.motion.move = Vec3(values[0], values[1], values[2]);
            extras.motion.rotate = Vec3(values[3], values[4], values[5]);
            extras.motion.scale = values[6];
            extras.motion.period = values[7] > 0 ? values[7] : 4;
            extras.hasMotion = true;
        }
        else if (word == "act")
        {
            if (!scene_parse::readQuoted(tail, index, extras.action.target))
            {
                error = "act needs a quoted target name";
                return false;
            }
            double x = 0, y = 0, z = 0;
            if (!readDoubleAt(tail, index, x) || !readDoubleAt(tail, index, y) || !readDoubleAt(tail, index, z))
            {
                error = "act needs a move offset";
                return false;
            }
            extras.action.move = Vec3(x, y, z);
            double rx = 0, ry = 0, rz = 0;
            const size_t mark = index;
            if (readDoubleAt(tail, index, rx) && readDoubleAt(tail, index, ry) && readDoubleAt(tail, index, rz))
                extras.action.rotate = Vec3(rx, ry, rz);
            else
                index = mark;
            extras.hasAction = true;
        }
        else if (word == "parent")
        {
            double parent = 0;
            if (!readDoubleAt(tail, index, parent))
            {
                error = "parent needs an id";
                return false;
            }
            extras.parentId = static_cast<int>(parent);
        }
        else if (word == "prefab")
        {
            if (!scene_parse::readQuoted(tail, index, extras.prefab))
            {
                error = "prefab needs a quoted name";
                return false;
            }
        }
        else if (word == "layer")
        {
            double layer = 0;
            if (!readDoubleAt(tail, index, layer))
            {
                error = "layer needs a number";
                return false;
            }
            extras.layer = static_cast<int>(layer);
        }
        else if (word == "instance")
        {
            if (!scene_parse::readQuoted(tail, index, extras.instanceOf))
            {
                error = "instance needs a quoted name";
                return false;
            }
        }
        else if (word == "every")
        {
            if (!readDoubleAt(tail, index, extras.spawnEvery))
            {
                error = "every needs a number of seconds";
                return false;
            }
        }
        else
        {
            error = "unknown option " + word;
            return false;
        }
    }
    return true;
}

bool readQuoted(const std::string &line, size_t &index, std::string &text)
{
    while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index])))
        ++index;
    if (index >= line.size() || line[index] != '"')
        return false;
    ++index;
    text.clear();
    while (index < line.size())
    {
        char character = line[index++];
        if (character == '\\')
        {
            if (index >= line.size())
                return false;
            text.push_back(line[index++]);
            continue;
        }
        if (character == '"')
            return true;
        text.push_back(character);
    }
    return false;
}

} // namespace scene_parse

namespace
{

void applySurface(Material &material, const scene_parse::SurfaceExtras &extras)
{
    material.transmission = extras.transmission;
    material.ior = extras.ior;
    material.uvScale = extras.uvScale > 0 ? extras.uvScale : 1;
    material.uvScrollU = extras.uvScrollU;
    material.uvScrollV = extras.uvScrollV;
    material.albedoMap = extras.texture;
    material.emission = extras.emission;
    material.normalMap = extras.normalMap;
    if (extras.hasRoughness)
        material.roughness = extras.roughness;
}

template <typename... Values>
bool readNumbers(std::istream &in, Values &...values)
{
    return (... && static_cast<bool>(in >> values));
}

Material materialFrom(double red, double green, double blue, double ambient, double diffuse, double specular, double shininess, double reflectivity)
{
    Material material;
    material.albedo = Vec3(red, green, blue);
    material.ambient = ambient;
    material.diffuse = diffuse;
    material.specular = specular;
    material.shininess = shininess;
    material.reflectivity = reflectivity;
    return material;
}


}

bool loadScene(const std::filesystem::path &path, Scene &scene, CameraSetup &camera, std::string &error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "Could not open " + path.string();
        return false;
    }

    Scene loaded;
    CameraSetup loadedCamera = camera;
    bool sawHeader = false;
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line))
    {
        ++lineNumber;
        std::istringstream head(line);
        std::string command;
        if (!(head >> command))
            continue;
        if (!command.empty() && command[0] == '#')
            continue;

        auto fail = [&](const std::string &reason) {
            error = path.string() + ":" + std::to_string(lineNumber) + ": " + reason;
            return false;
        };

        if (command == "raytracer-scene")
        {
            int version = 0;
            if (!(head >> version) || version != 1)
                return fail("unsupported scene version");
            sawHeader = true;
            continue;
        }
        if (!sawHeader)
            return fail("missing raytracer-scene header");

        if (command == "ambient")
        {
            double x = 0, y = 0, z = 0;
            if (!readNumbers(head, x, y, z))
                return fail("ambient needs 3 numbers");
            loaded.setAmbient(Vec3(x, y, z));
            continue;
        }
        if (command == "background")
        {
            double hx = 0, hy = 0, hz = 0, zx = 0, zy = 0, zz = 0;
            if (!readNumbers(head, hx, hy, hz, zx, zy, zz))
                return fail("background needs 6 numbers");
            loaded.setBackground(Vec3(hx, hy, hz), Vec3(zx, zy, zz));
            continue;
        }
        if (command == "camera")
        {
            double fx = 0, fy = 0, fz = 0, ax = 0, ay = 0, az = 0, fov = 0;
            if (!readNumbers(head, fx, fy, fz, ax, ay, az, fov))
                return fail("camera needs 7 numbers");
            loadedCamera.lookFrom = Vec3(fx, fy, fz);
            loadedCamera.lookAt = Vec3(ax, ay, az);
            loadedCamera.up = Vec3(0, 1, 0);
            loadedCamera.fovY = fov;
            double aperture = 0;
            double focus = 0;
            if (head >> aperture)
                loadedCamera.aperture = aperture > 0 ? aperture : 0;
            if (head >> focus)
                loadedCamera.focusDistance = focus > 0 ? focus : 0;
            continue;
        }
        if (command == "fog")
        {
            double r = 0, g = 0, b = 0, density = 0;
            if (!readNumbers(head, r, g, b, density))
                return fail("fog needs a color and a density");
            loaded.setFog(Vec3(r, g, b), density);
            continue;
        }
        if (command == "exposure")
        {
            double exposure = 1;
            if (!(head >> exposure))
                return fail("exposure needs a number");
            loaded.setExposure(exposure);
            continue;
        }
        if (command == "shot")
        {
            size_t index = line.find(command);
            index = index == std::string::npos ? line.size() : index + command.size();
            std::string name;
            if (!scene_parse::readQuoted(line, index, name))
                return fail("shot needs a quoted name");
            std::stringstream rest(line.substr(index));
            double fx = 0, fy = 0, fz = 0, ax = 0, ay = 0, az = 0, fov = 42;
            if (!(rest >> fx >> fy >> fz >> ax >> ay >> az >> fov))
                return fail("shot needs a name and 7 numbers");
            SceneCamera shot;
            shot.name = name;
            shot.lookFrom = Vec3(fx, fy, fz);
            shot.lookAt = Vec3(ax, ay, az);
            shot.fov = fov;
            loaded.shots().push_back(shot);
            continue;
        }
        if (command == "environment")
        {
            size_t index = line.find(command);
            index = index == std::string::npos ? line.size() : index + command.size();
            std::string image;
            if (!scene_parse::readQuoted(line, index, image))
                return fail("environment needs a quoted path");
            loaded.setEnvironment(image);
            continue;
        }

        size_t index = line.find(command);
        index = index == std::string::npos ? line.size() : index + command.size();
        std::string name;
        if (command == "sphere" || command == "plane" || command == "mesh" || command == "light")
        {
            if (!scene_parse::readQuoted(line, index, name))
                return fail("expected a quoted name");
        }
        else
        {
            return fail("unknown command " + command);
        }

        std::istringstream rest(line.substr(index));
        if (command == "sphere")
        {
            double cx = 0, cy = 0, cz = 0, radius = 0;
            double red = 0, green = 0, blue = 0, ambient = 0, diffuse = 0, specular = 0, shininess = 0, reflectivity = 0;
            if (!readNumbers(rest, cx, cy, cz, radius, red, green, blue, ambient, diffuse, specular, shininess, reflectivity))
                return fail("sphere needs a center, radius, and material");
            std::string tail;
            std::getline(rest, tail);
            scene_parse::SurfaceExtras extras;
            std::string tailError;
            if (!scene_parse::parseTail(tail, extras, tailError))
                return fail(tailError);
            Material material = materialFrom(red, green, blue, ambient, diffuse, specular, shininess, reflectivity);
            applySurface(material, extras);
            auto sphere = std::make_unique<Sphere>(Vec3(cx, cy, cz), radius, material);
            sphere->name = name;
            sphere->setTag(extras.tag);
            if (extras.hasMotion)
                sphere->motion = extras.motion;
            if (extras.hasAction)
                sphere->action = extras.action;
            sphere->spawnEvery = extras.spawnEvery;
            sphere->parentId = extras.parentId;
            sphere->prefab = extras.prefab;
            sphere->instanceOf = extras.instanceOf;
            sphere->layer = extras.layer;
            loaded.add(std::move(sphere));
            continue;
        }
        if (command == "plane")
        {
            double px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
            double red = 0, green = 0, blue = 0, ambient = 0, diffuse = 0, specular = 0, shininess = 0, reflectivity = 0;
            if (!readNumbers(rest, px, py, pz, nx, ny, nz, red, green, blue, ambient, diffuse, specular, shininess, reflectivity))
                return fail("plane needs a point, normal, and material");
            std::string tail;
            std::getline(rest, tail);
            scene_parse::SurfaceExtras extras;
            std::string tailError;
            if (!scene_parse::parseTail(tail, extras, tailError))
                return fail(tailError);
            Material material = materialFrom(red, green, blue, ambient, diffuse, specular, shininess, reflectivity);
            applySurface(material, extras);
            auto plane = std::make_unique<Plane>(Vec3(px, py, pz), Vec3(nx, ny, nz), material);
            plane->name = name;
            plane->setTag(extras.tag);
            if (extras.hasMotion)
                plane->motion = extras.motion;
            if (extras.hasAction)
                plane->action = extras.action;
            plane->spawnEvery = extras.spawnEvery;
            if (extras.checker)
                plane->setChecker(extras.checkerAlbedo, extras.checkerScale);
            plane->parentId = extras.parentId;
            plane->prefab = extras.prefab;
            plane->instanceOf = extras.instanceOf;
            plane->layer = extras.layer;
            loaded.add(std::move(plane));
            continue;
        }
        if (command == "mesh")
        {
            double px = 0, py = 0, pz = 0, scale = 1;
            double red = 0, green = 0, blue = 0, ambient = 0, diffuse = 0, specular = 0, shininess = 0, reflectivity = 0;
            if (!readNumbers(rest, px, py, pz, scale, red, green, blue, ambient, diffuse, specular, shininess, reflectivity))
                return fail("mesh needs a position, scale, and material");
            std::string tail;
            std::getline(rest, tail);
            scene_parse::SurfaceExtras extras;
            std::string tailError;
            if (!scene_parse::parseTail(tail, extras, tailError))
                return fail(tailError);
            if (extras.objectFile.empty())
                return fail("mesh needs an obj path");
            auto mesh = std::make_unique<Mesh>();
            std::string meshError;
            if (!mesh->load(std::filesystem::u8path(extras.objectFile), meshError))
                return fail(meshError);
            Material material = materialFrom(red, green, blue, ambient, diffuse, specular, shininess, reflectivity);
            applySurface(material, extras);
            mesh->setMaterial(material);
            mesh->setPosition(Vec3(px, py, pz));
            mesh->setScale(scale);
            mesh->setRotation(extras.rotation);
            mesh->name = name;
            mesh->setTag(extras.tag);
            if (extras.hasMotion)
                mesh->motion = extras.motion;
            if (extras.hasAction)
                mesh->action = extras.action;
            mesh->spawnEvery = extras.spawnEvery;
            mesh->parentId = extras.parentId;
            mesh->prefab = extras.prefab;
            mesh->instanceOf = extras.instanceOf;
            mesh->layer = extras.layer;
            loaded.add(std::move(mesh));
            continue;
        }

        double px = 0, py = 0, pz = 0, cr = 0, cg = 0, cb = 0, intensity = 0, falloff = 0;
        if (!readNumbers(rest, px, py, pz, cr, cg, cb, intensity, falloff))
            return fail("light needs a position, color, intensity, and falloff");
        std::string tail;
        std::getline(rest, tail);
        scene_parse::SurfaceExtras extras;
        std::string tailError;
        if (!scene_parse::parseTail(tail, extras, tailError, true))
            return fail(tailError);
        loaded.addLight(PointLight(Vec3(px, py, pz), Vec3(cr, cg, cb), intensity, falloff));
        loaded.lights().back().name = name;
        loaded.lights().back().radius = extras.radius;
        loaded.lights().back().directional = extras.directional;
        loaded.lights().back().spotDirection = extras.spotDirection;
        loaded.lights().back().spotOuter = extras.spotOuter;
        loaded.lights().back().spotInner = extras.spotInner;
    }

    if (!sawHeader)
    {
        error = path.string() + ": missing raytracer-scene header";
        return false;
    }

    scene = std::move(loaded);
    camera = loadedCamera;
    return true;
}

