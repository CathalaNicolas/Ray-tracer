#pragma once

#include "MeshGeometry.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mesh_obj
{

bool loadTriangles(const std::filesystem::path &path, std::vector<MeshTri> &triangles, std::string &error);
void fitToGround(std::vector<MeshTri> &triangles);
std::shared_ptr<MeshGeometry> load(const std::filesystem::path &path, std::string &error);

} // namespace mesh_obj
