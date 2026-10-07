#pragma once

#include "Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

inline std::uint8_t encodeChannel(double channel)
{
    if (!std::isfinite(channel))
        channel = 0;
    channel = std::clamp(channel, 0.0, 1.0);
    double srgb = std::pow(channel, 1.0 / 2.2);
    int value = static_cast<int>(std::lround(srgb * 255.0));
    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;
    return static_cast<std::uint8_t>(value);
}

class Image
{
public:
    Image(int width, int height)
        : width_(width), height_(height), pixels_(static_cast<size_t>(width) * static_cast<size_t>(height))
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    void set(int x, int y, const Vec3 &color)
    {
        pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)] = color;
    }

    const Vec3 &at(int x, int y) const
    {
        return pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)];
    }

    std::vector<std::uint8_t> toRGBA() const
    {
        std::vector<std::uint8_t> bytes(pixels_.size() * 4);
        for (size_t i = 0; i < pixels_.size(); ++i)
        {
            bytes[i * 4] = encodeChannel(pixels_[i].x);
            bytes[i * 4 + 1] = encodeChannel(pixels_[i].y);
            bytes[i * 4 + 2] = encodeChannel(pixels_[i].z);
            bytes[i * 4 + 3] = 255;
        }
        return bytes;
    }

    bool writePPM(const std::string &path) const
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            return false;

        out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
        for (const Vec3 &pixel : pixels_)
        {
            unsigned char rgb[3] = {
                encodeChannel(pixel.x),
                encodeChannel(pixel.y),
                encodeChannel(pixel.z)};
            out.write(reinterpret_cast<const char *>(rgb), 3);
        }
        return static_cast<bool>(out);
    }

private:
    int width_;
    int height_;
    std::vector<Vec3> pixels_;
};
