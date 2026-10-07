#include "SceneFile.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

void copyPrefabFields(Hittable &destination, const Hittable &source)
{
    destination.setMaterial(source.material());
    destination.setTag(source.tag);
    destination.layer = source.layer;
    destination.motion = source.motion;
    destination.copyShapeFrom(source);
}

void mix(std::uint64_t &hash, std::uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void mixDouble(std::uint64_t &hash, double value)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "fingerprint packs each double as 64 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    mix(hash, bits);
}

void mixVec3(std::uint64_t &hash, const Vec3 &value)
{
    mixDouble(hash, value.x);
    mixDouble(hash, value.y);
    mixDouble(hash, value.z);
}

void mixString(std::uint64_t &hash, const std::string &text)
{
    for (unsigned char character : text)
        mix(hash, character);
}

void mixMaterial(std::uint64_t &hash, const Material &material)
{
    mixVec3(hash, material.albedo);
    mixDouble(hash, material.ambient);
    mixDouble(hash, material.diffuse);
    mixDouble(hash, material.specular);
    mixDouble(hash, material.shininess);
    mixDouble(hash, material.reflectivity);
    mixDouble(hash, material.transmission);
    mixDouble(hash, material.ior);
    mixDouble(hash, material.uvScale);
    mixDouble(hash, material.uvScrollU);
    mixDouble(hash, material.uvScrollV);
    mixDouble(hash, material.roughness);
    mixDouble(hash, material.emission);
    mixString(hash, material.albedoMap);
    mixString(hash, material.normalMap);
}

void mixMotion(std::uint64_t &hash, const Motion &motion)
{
    mixVec3(hash, motion.move);
    mixVec3(hash, motion.rotate);
    mixDouble(hash, motion.scale);
    mixDouble(hash, motion.period);
}

void mixCopiedSource(std::uint64_t &hash, const Hittable &source)
{
    mixMaterial(hash, source.material());
    mixString(hash, source.tag);
    mix(hash, static_cast<std::uint64_t>(source.layer));
    mixMotion(hash, source.motion);
    source.mixShapeHash(hash);
}

std::uint64_t sourceFingerprint(const Hittable &source, const std::vector<Hittable *> &children)
{
    std::uint64_t hash = 0;
    mixString(hash, source.kind());
    mixCopiedSource(hash, source);
    mix(hash, children.size());
    for (const Hittable *child : children)
    {
        mixString(hash, child->kind());
        mixCopiedSource(hash, *child);
        mixVec3(hash, child->localPosition());
    }
    return hash;
}

}

void syncPrefabInstances(Scene &scene)
{
    struct Applied
    {
        int id = 0;
        std::uint64_t hash = 0;
    };
    static std::unordered_map<const Hittable *, Applied> applied;

    for (auto entry = applied.begin(); entry != applied.end();)
    {
        bool live = false;
        for (const auto &object : scene.objects())
        {
            if (object.get() == entry->first)
            {
                live = true;
                break;
            }
        }
        if (!live)
            entry = applied.erase(entry);
        else
            ++entry;
    }

    for (const auto &object : scene.objects())
    {
        if (object->instanceOf.empty())
            continue;
        const Hittable *source = nullptr;
        for (const auto &candidate : scene.objects())
        {
            if (candidate->prefab == object->instanceOf)
            {
                source = candidate.get();
                break;
            }
        }
        if (source == nullptr || std::string(source->kind()) != object->kind())
            continue;
        std::vector<Hittable *> sourceChildren;
        std::vector<Hittable *> instanceChildren;
        for (const auto &candidate : scene.objects())
        {
            if (candidate->parentId == source->id)
                sourceChildren.push_back(candidate.get());
            else if (candidate->parentId == object->id)
                instanceChildren.push_back(candidate.get());
        }
        const std::uint64_t hash = sourceFingerprint(*source, sourceChildren);
        const auto found = applied.find(object.get());
        if (found != applied.end() && found->second.id == object->id && found->second.hash == hash)
            continue;
        copyPrefabFields(*object, *source);
        const size_t count = std::min(sourceChildren.size(), instanceChildren.size());
        for (size_t index = 0; index < count; ++index)
        {
            if (std::string(sourceChildren[index]->kind()) != instanceChildren[index]->kind())
                continue;
            copyPrefabFields(*instanceChildren[index], *sourceChildren[index]);
            instanceChildren[index]->setLocalPosition(sourceChildren[index]->localPosition());
        }
        applied[object.get()] = Applied{object->id, hash};
    }
}

int placePrefabInstance(Scene &scene, int sourceId)
{
    Hittable *source = scene.find(sourceId);
    if (source == nullptr || source->prefab.empty())
        return -1;
    std::vector<const Hittable *> children;
    for (const auto &object : scene.objects())
    {
        if (object->parentId == source->id)
            children.push_back(object.get());
    }
    auto root = source->clone();
    root->prefab.clear();
    root->instanceOf = source->prefab;
    root->id = 0;
    root->parentId = 0;
    root->setLocalPosition(root->localPosition() + Vec3(0.8, 0, 0.8));
    const int rootId = scene.add(std::move(root));
    for (const Hittable *child : children)
    {
        auto copy = child->clone();
        copy->id = 0;
        copy->parentId = rootId;
        copy->prefab.clear();
        copy->instanceOf.clear();
        scene.add(std::move(copy));
    }
    return rootId;
}
