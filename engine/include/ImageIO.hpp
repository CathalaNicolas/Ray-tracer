#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct LoadedImage
{
    int width = 0;
    int height = 0;
    bool hdr = false;
    std::vector<unsigned char> rgba8;
    std::vector<float> rgba32;
};

bool loadImage(const std::filesystem::path &path, LoadedImage &image, std::string &error);
