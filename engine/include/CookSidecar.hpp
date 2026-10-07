#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Cooker dependency sidecar next to a cooked output (`output.rtm.d`).
// Line 1: `src <sha256> <source-path>`
// Later:  `dep <sha256> <dep-path>`

std::filesystem::path cookSidecarPath(const std::filesystem::path &cookedOutput);
bool cookNeedsUpdate(const std::filesystem::path &output, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps);
bool cookWriteSidecar(const std::filesystem::path &output, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps, std::string &error);

// True when the sidecar next to `cookedVirtual` (PhysFS or disk) matches `source` and its deps.
// Missing sidecar or hash mismatch returns false.
bool cookedSidecarMatches(const std::string &cookedVirtual, const std::filesystem::path &source,
    const std::vector<std::filesystem::path> &deps);

// OBJ `.mtl` and texture maps referenced from it (same rules as the cooker).
void collectMeshSourceDeps(const std::filesystem::path &source, std::vector<std::filesystem::path> &deps);
