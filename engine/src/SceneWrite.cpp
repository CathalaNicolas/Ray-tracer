#include "SceneWrite.hpp"

#include <cmath>

namespace scene_write
{

std::string escapeName(const std::string &name)
{
    std::string escaped;
    for (char character : name)
    {
        if (character == '\n' || character == '\r')
            continue;
        if (character == '\\' || character == '"')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    return escaped;
}

void writeTag(std::ostream &out, const std::string &tag)
{
    if (!tag.empty())
        out << " tag \"" << escapeName(tag) << '"';
}

void writeMotion(std::ostream &out, const Hittable &object)
{
    if (object.motion.active())
    {
        const Motion &motion = object.motion;
        out << " motion " << motion.move.x << ' ' << motion.move.y << ' ' << motion.move.z << ' '
            << motion.rotate.x << ' ' << motion.rotate.y << ' ' << motion.rotate.z << ' '
            << motion.scale << ' ' << motion.period;
    }
    if (object.spawnEvery > 0)
        out << " every " << object.spawnEvery;
}

void writeAction(std::ostream &out, const Hittable &object)
{
    if (!object.action.armed())
        return;
    const Action &action = object.action;
    out << " act \"" << escapeName(action.target) << "\" " << action.move.x << ' ' << action.move.y << ' ' << action.move.z;
    const double spin = std::abs(action.rotate.x) + std::abs(action.rotate.y) + std::abs(action.rotate.z);
    if (spin > 1e-8)
        out << ' ' << action.rotate.x << ' ' << action.rotate.y << ' ' << action.rotate.z;
}

void writeParent(std::ostream &out, const Hittable &object)
{
    if (object.parentId != 0)
        out << " parent " << object.parentId;
    if (!object.prefab.empty())
        out << " prefab \"" << escapeName(object.prefab) << '"';
    if (!object.instanceOf.empty())
        out << " instance \"" << escapeName(object.instanceOf) << '"';
    if (object.layer != 0)
        out << " layer " << object.layer;
}

void writeMaterial(std::ostream &out, const Material &material)
{
    out << material.albedo.x << ' ' << material.albedo.y << ' ' << material.albedo.z << ' '
        << material.ambient << ' ' << material.diffuse << ' ' << material.specular << ' '
        << material.shininess << ' ' << material.reflectivity
        << " trans " << material.transmission << ' ' << material.ior
        << " uv " << material.uvScale;
    if (material.roughness >= 0)
        out << " rough " << material.roughness;
    if (material.emission != 0)
        out << " emit " << material.emission;
    if (!material.albedoMap.empty())
        out << " texture \"" << escapeName(material.albedoMap) << '"';
    if (!material.normalMap.empty())
        out << " normal \"" << escapeName(material.normalMap) << '"';
    if (material.uvScrollU != 0 || material.uvScrollV != 0)
        out << " scroll " << material.uvScrollU << ' ' << material.uvScrollV;
}

}
