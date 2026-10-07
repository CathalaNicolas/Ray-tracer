#pragma once

#include "MeshGeometry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

inline constexpr char kRtmMagic[4] = {'R', 'T', 'M', '1'};
// v4: v3 plus material refs and submesh index ranges.
// v3: one shared vertex buffer, then per-LOD index lists (cooker indices kept).
// v1/v2 still decode (each lod had its own vertex list, often a triangle soup).
inline constexpr std::uint32_t kRtmPayloadVersion = 4;

bool encodeMeshGeometry(const MeshGeometry &geometry, std::vector<std::uint8_t> &out, std::string &error);
bool decodeMeshGeometry(const std::vector<std::uint8_t> &bytes, MeshGeometry &geometry, std::string &error);
bool writeRtmFile(const std::filesystem::path &path, const MeshGeometry &geometry, std::string &error);
std::shared_ptr<MeshGeometry> loadRtmFile(const std::filesystem::path &path, std::string &error);
bool isRtmPath(const std::filesystem::path &path);
