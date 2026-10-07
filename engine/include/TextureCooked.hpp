#pragma once

#include "ImageIO.hpp"

#include <filesystem>
#include <string>

bool loadDdsFile(const std::filesystem::path &path, LoadedImage &image, std::string &error);
bool isDdsPath(const std::filesystem::path &path);
