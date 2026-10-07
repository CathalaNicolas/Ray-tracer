#pragma once

#include "CookSidecar.hpp"

#include <filesystem>
#include <string>
#include <vector>

void cookCollectObjDeps(const std::filesystem::path &obj, std::vector<std::filesystem::path> &deps);
bool cookWriteManifest(const std::filesystem::path &cookedDir, std::string &error);
