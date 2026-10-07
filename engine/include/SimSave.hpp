#pragma once

#include "SimSerialize.hpp"

#include <filesystem>
#include <vector>

// One SQLite play-session file (WAL + a single IMMEDIATE transaction).
// Default path is the working directory, next to raytracer-settings.txt.
// Mesh triangle soup is not stored; MeshShape::sourcePath is inside the W6 blob.

inline constexpr std::uint16_t kPlaySessionDbVersion = 1;
inline constexpr const char *kDefaultPlaySessionFileName = "raytracer-play.sqlite";

inline std::filesystem::path defaultPlaySessionPath()
{
    return std::filesystem::path(kDefaultPlaySessionFileName);
}

struct PlaySessionInfo
{
    SimTick tick = 0;
    Vec3 lastLook{0, 0, -1};
    std::uint32_t seed = 1;
    std::vector<Command> commands;
};

bool savePlaySession(const std::filesystem::path &path, const Scene &scene, const PlayState &state, SimTick tick,
    const Vec3 &lastLook, const std::vector<Command> &commands);

bool loadPlaySession(const std::filesystem::path &path, Scene &scene, PlayState &state, PlaySessionInfo &info);
