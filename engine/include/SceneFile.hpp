#pragma once

#include "DemoScene.hpp"

#include <filesystem>
#include <string>

bool saveScene(const std::filesystem::path &path, const Scene &scene, const CameraSetup &camera, std::string &error);
bool loadScene(const std::filesystem::path &path, Scene &scene, CameraSetup &camera, std::string &error);
