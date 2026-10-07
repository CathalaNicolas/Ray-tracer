#pragma once

#include "MeshGeometry.hpp"

#include <filesystem>
#include <memory>
#include <string>

// OBJ lives in core. FBX is registered from the editor via mesh_import::load.
using MeshFileLoader = std::shared_ptr<MeshGeometry> (*)(const std::filesystem::path &path, std::string &error);

void setMeshFileLoader(MeshFileLoader loader);

// When false (shipping), only `.rtm` / cooked mirrors load; source OBJ/FBX are refused.
// Defaults to true so tests and the editor can parse sources. The editor sets this from CMake
// (`RAYTRACER_SHIPPING`); call setAllowSourceMeshes(false) for a cooked-only game build.
void setAllowSourceMeshes(bool allow);
bool allowSourceMeshes();

std::shared_ptr<MeshGeometry> loadMeshFile(const std::filesystem::path &path, std::string &error);
bool refreshMeshFile(std::shared_ptr<MeshGeometry> &geometry, std::string &sourcePath, std::int64_t &sourceStamp,
    std::uintmax_t &sourceBytes, std::string &error);
