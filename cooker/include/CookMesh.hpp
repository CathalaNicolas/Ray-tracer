#pragma once

#include <filesystem>
#include <string>

bool cookSourceToRtm(const std::filesystem::path &source, const std::filesystem::path &output, std::string &error,
    bool ifNewer);
int cookDirectoryToRtm(const std::filesystem::path &sourceDir, const std::filesystem::path &outputDir, bool ifNewer,
    std::string &error);
bool cookImageToDds(const std::filesystem::path &source, const std::filesystem::path &output, bool normalMap,
    std::string &error);
bool cookCatalogJson(const std::filesystem::path &jsonPath, const std::filesystem::path &output, std::string &error);
bool packCookedZip(const std::filesystem::path &cookedDir, const std::filesystem::path &zipPath, std::string &error);
