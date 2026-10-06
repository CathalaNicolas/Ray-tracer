#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "ImageIO.hpp"

#include <cstdio>

bool loadImage(const std::filesystem::path &path, LoadedImage &image, std::string &error)
{
    image = {};
#ifdef _WIN32
    FILE *file = _wfopen(path.wstring().c_str(), L"rb");
#else
    FILE *file = std::fopen(path.string().c_str(), "rb");
#endif
    if (file == nullptr)
    {
        error = "Could not open " + path.string();
        return false;
    }

    const bool hdr = stbi_is_hdr_from_file(file) != 0;
    std::rewind(file);
    int width = 0;
    int height = 0;
    int components = 0;
    if (hdr)
    {
        float *pixels = stbi_loadf_from_file(file, &width, &height, &components, 4);
        std::fclose(file);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            error = "Could not read " + path.string();
            return false;
        }
        image.hdr = true;
        image.width = width;
        image.height = height;
        image.rgba32.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        stbi_image_free(pixels);
        return true;
    }

    unsigned char *pixels = stbi_load_from_file(file, &width, &height, &components, 4);
    std::fclose(file);
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        error = "Could not read " + path.string();
        return false;
    }
    image.width = width;
    image.height = height;
    image.rgba8.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);
    return true;
}
