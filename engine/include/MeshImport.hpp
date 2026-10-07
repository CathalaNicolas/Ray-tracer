#pragma once

#include "MeshGeometry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace mesh_import
{

std::shared_ptr<MeshGeometry> load(const std::filesystem::path &path, std::string &error);
bool refresh(std::shared_ptr<MeshGeometry> &geometry, std::string &sourcePath, std::int64_t &sourceStamp, std::uintmax_t &sourceBytes, std::string &error);

// Writes a one-triangle FBX for self-tests. Keeps the FBX SDK out of render TUs.
bool writeTestTriangleFbx(const std::filesystem::path &path);

} // namespace mesh_import
