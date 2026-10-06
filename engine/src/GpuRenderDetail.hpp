#pragma once

#include "GpuContribute.hpp"
#include "ImageIO.hpp"
#include "Material.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gpu_detail
{

constexpr int kDataWidth = 1024;
constexpr int kAlbedoEdge = 256;

void pushVec3(std::vector<float> &values, const Vec3 &v);
unsigned short floatToHalf(float value);
void uploadDataTexture(unsigned &texture, const std::vector<float> &texels, std::vector<float> &cached, int &cachedHeight, std::uint64_t &cachedHash, std::size_t &cachedCount);
void beginImageStats();
void setReuseShadowMaps(bool reuse);
bool reuseShadowMaps();

struct ImageCache
{
    std::filesystem::file_time_type stamp{};
    std::uintmax_t bytes = 0;
    bool missing = false;
    LoadedImage image;
};

ImageCache &cachedImage(const std::string &path);
std::vector<unsigned char> scaledAlbedo(const LoadedImage &image);
void pushOpt(std::vector<float> &values, const Material &material, int layer);
float shadeExponent(const Material &material);

}
