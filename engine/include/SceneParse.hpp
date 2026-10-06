#pragma once

#include "Hittable.hpp"

#include <string>

namespace scene_parse
{

struct SurfaceExtras
{
    double transmission = 0;
    double ior = 1.5;
    double uvScale = 1;
    double uvScrollU = 0;
    double uvScrollV = 0;
    std::string texture;
    std::string objectFile;
    bool checker = false;
    Vec3 checkerAlbedo;
    double checkerScale = 1;
    double radius = 0;
    bool directional = false;
    Vec3 spotDirection;
    double spotOuter = 0;
    double spotInner = 0;
    bool hasRoughness = false;
    double roughness = -1;
    double emission = 0;
    std::string normalMap;
    Vec3 rotation;
    std::string tag;
    Motion motion;
    double spawnEvery = 0;
    bool hasMotion = false;
    Action action;
    bool hasAction = false;
    int parentId = 0;
    std::string prefab;
    std::string instanceOf;
    int layer = 0;
};

bool readQuoted(const std::string &line, size_t &index, std::string &text);
bool readWord(const std::string &text, size_t &index, std::string &word);
bool readDoubleAt(const std::string &text, size_t &index, double &value);
bool parseTail(const std::string &tail, SurfaceExtras &extras, std::string &error, bool lights = false);

}
