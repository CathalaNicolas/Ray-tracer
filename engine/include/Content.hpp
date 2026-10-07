#pragma once

#include "Catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// PhysFS mounts `cooked/` then `cooked/pak0.zip` (later entries override).
// Missing folders are fine. Unmounted, reads fall back to the real filesystem.
// On init, loads `cooked/catalog.bin` into contentCatalog() when present.
bool contentInit(const std::filesystem::path &root);
void contentShutdown();
bool contentReady();
bool contentExists(const std::string &virtualPath);
bool contentRead(const std::string &virtualPath, std::vector<std::uint8_t> &bytes, std::string &error);
std::string cookedMeshVirtualPath(const std::filesystem::path &sourcePath);

const Catalog &contentCatalog();
bool contentCatalogLoaded();
