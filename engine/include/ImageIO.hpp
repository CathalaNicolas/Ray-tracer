#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class GpuTexFormat : std::uint8_t
{
    Rgba8 = 0,
    Bc1 = 1,
    Bc2 = 2,
    Bc3 = 3,
    Bc5 = 4,
    Bc7 = 5,
};

struct ImageMip
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bytes;
};

struct LoadedImage
{
    int width = 0;
    int height = 0;
    bool hdr = false;
    bool compressed = false;
    GpuTexFormat format = GpuTexFormat::Rgba8;
    std::vector<unsigned char> rgba8;
    std::vector<float> rgba32;
    std::vector<ImageMip> mips;
};

bool loadImage(const std::filesystem::path &path, LoadedImage &image, std::string &error);
