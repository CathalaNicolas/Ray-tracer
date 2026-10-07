#include "TextureCooked.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{

constexpr std::uint32_t kDdsMagic = 0x20534444u; // "DDS "
constexpr std::uint32_t kDdsFourCcDx10 = 0x30315844u; // "DX10"
constexpr std::uint32_t kDdsFourCcDxt1 = 0x31545844u;
constexpr std::uint32_t kDdsFourCcDxt3 = 0x33545844u;
constexpr std::uint32_t kDdsFourCcDxt5 = 0x35545844u;
constexpr std::uint32_t kDdsFourCcBc4U = 0x55344342u; // "BC4U"
constexpr std::uint32_t kDdsFourCcAti2 = 0x32495441u; // "ATI2"
constexpr std::uint32_t kDdsFourCcBc5U = 0x55354342u; // "BC5U"
constexpr std::uint32_t kDdpfFourCc = 0x4u;
constexpr std::uint32_t kDdpfRgb = 0x40u;
constexpr std::uint32_t kDdpfAlphaPixels = 0x1u;

std::uint32_t readU32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::size_t blockBytes(GpuTexFormat format)
{
    switch (format)
    {
    case GpuTexFormat::Bc1:
        return 8;
    case GpuTexFormat::Bc2:
    case GpuTexFormat::Bc3:
    case GpuTexFormat::Bc5:
    case GpuTexFormat::Bc7:
        return 16;
    default:
        return 0;
    }
}

std::size_t mipByteCount(int width, int height, GpuTexFormat format)
{
    if (format == GpuTexFormat::Rgba8)
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    const int blocksX = std::max(1, (width + 3) / 4);
    const int blocksY = std::max(1, (height + 3) / 4);
    return static_cast<std::size_t>(blocksX) * static_cast<std::size_t>(blocksY) * blockBytes(format);
}

bool fourCcToFormat(std::uint32_t fourCc, GpuTexFormat &format)
{
    if (fourCc == kDdsFourCcDxt1)
    {
        format = GpuTexFormat::Bc1;
        return true;
    }
    if (fourCc == kDdsFourCcDxt3)
    {
        format = GpuTexFormat::Bc2;
        return true;
    }
    if (fourCc == kDdsFourCcDxt5)
    {
        format = GpuTexFormat::Bc3;
        return true;
    }
    if (fourCc == kDdsFourCcAti2 || fourCc == kDdsFourCcBc5U)
    {
        format = GpuTexFormat::Bc5;
        return true;
    }
    if (fourCc == kDdsFourCcBc4U)
        return false;
    return false;
}

bool dxgiToFormat(std::uint32_t dxgi, GpuTexFormat &format, bool &compressed)
{
    compressed = true;
    switch (dxgi)
    {
    case 71: // DXGI_FORMAT_BC1_UNORM
    case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
        format = GpuTexFormat::Bc1;
        return true;
    case 74: // BC2
    case 75:
        format = GpuTexFormat::Bc2;
        return true;
    case 77: // BC3
    case 78:
        format = GpuTexFormat::Bc3;
        return true;
    case 83: // BC5_UNORM
    case 84:
        format = GpuTexFormat::Bc5;
        return true;
    case 98: // BC7_UNORM
    case 99: // BC7_UNORM_SRGB
        format = GpuTexFormat::Bc7;
        return true;
    case 28: // R8G8B8A8_UNORM
    case 29: // R8G8B8A8_UNORM_SRGB
        format = GpuTexFormat::Rgba8;
        compressed = false;
        return true;
    default:
        return false;
    }
}

} // namespace

bool isDdsPath(const std::filesystem::path &path)
{
    std::string ext = path.extension().string();
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".dds";
}

bool loadDdsFile(const std::filesystem::path &path, LoadedImage &image, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "could not open DDS";
        return false;
    }
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 128)
    {
        error = "DDS too small";
        return false;
    }
    if (readU32(file.data()) != kDdsMagic)
    {
        error = "not a DDS file";
        return false;
    }
    const std::uint8_t *header = file.data() + 4;
    const std::uint32_t height = readU32(header + 8);
    const std::uint32_t width = readU32(header + 12);
    const std::uint32_t mipCountHeader = readU32(header + 24);
    const std::uint8_t *pf = header + 72; // after dwReserved1[11]
    const std::uint32_t pfFlags = readU32(pf + 4);
    const std::uint32_t fourCc = readU32(pf + 8);
    const std::uint32_t rgbBitCount = readU32(pf + 12);
    const std::uint32_t rMask = readU32(pf + 16);
    const std::uint32_t gMask = readU32(pf + 20);
    const std::uint32_t bMask = readU32(pf + 24);
    const std::uint32_t aMask = readU32(pf + 28);

    if (width == 0 || height == 0)
    {
        error = "DDS zero size";
        return false;
    }

    GpuTexFormat format = GpuTexFormat::Rgba8;
    bool compressed = false;
    std::size_t dataOffset = 128;
    int mipCount = mipCountHeader > 0 ? static_cast<int>(mipCountHeader) : 1;

    if ((pfFlags & kDdpfFourCc) != 0 && fourCc == kDdsFourCcDx10)
    {
        if (file.size() < 148)
        {
            error = "DDS DX10 header truncated";
            return false;
        }
        const std::uint32_t dxgi = readU32(file.data() + 128);
        if (!dxgiToFormat(dxgi, format, compressed))
        {
            error = "unsupported DXGI format";
            return false;
        }
        dataOffset = 148;
    }
    else if ((pfFlags & kDdpfFourCc) != 0)
    {
        if (!fourCcToFormat(fourCc, format))
        {
            error = "unsupported DDS FourCC";
            return false;
        }
        compressed = true;
    }
    else if ((pfFlags & kDdpfRgb) != 0 && rgbBitCount == 32 && rMask == 0x000000ffu && gMask == 0x0000ff00u
        && bMask == 0x00ff0000u && ((pfFlags & kDdpfAlphaPixels) == 0 || aMask == 0xff000000u))
    {
        format = GpuTexFormat::Rgba8;
        compressed = false;
    }
    else
    {
        error = "unsupported DDS pixel format";
        return false;
    }

    image = {};
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.hdr = false;
    image.compressed = compressed;
    image.format = format;

    std::size_t cursor = dataOffset;
    int mipW = image.width;
    int mipH = image.height;
    for (int mip = 0; mip < mipCount; ++mip)
    {
        const std::size_t bytes = mipByteCount(mipW, mipH, format);
        if (cursor + bytes > file.size())
        {
            error = "DDS mip truncated";
            return false;
        }
        ImageMip level;
        level.width = mipW;
        level.height = mipH;
        level.bytes.assign(file.begin() + static_cast<std::ptrdiff_t>(cursor),
            file.begin() + static_cast<std::ptrdiff_t>(cursor + bytes));
        image.mips.push_back(std::move(level));
        cursor += bytes;
        mipW = std::max(1, mipW / 2);
        mipH = std::max(1, mipH / 2);
    }

    if (!compressed && !image.mips.empty())
    {
        image.rgba8 = image.mips[0].bytes;
    }
    return true;
}
