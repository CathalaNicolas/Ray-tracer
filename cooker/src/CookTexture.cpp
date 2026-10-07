#include "CookMesh.hpp"

#include <DirectXTex.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cstring>
#include <vector>

namespace
{

std::wstring wide(const std::filesystem::path &path)
{
    return path.wstring();
}

enum class CookedTextureKind
{
    Color = 0,
    Normal = 1,
};

bool writeDdsFromRgba8(const std::filesystem::path &path, int width, int height, const std::uint8_t *rgba,
    CookedTextureKind kind, std::string &error)
{
    if (width <= 0 || height <= 0 || rgba == nullptr)
    {
        error = "invalid image";
        return false;
    }
    DirectX::Image image = {};
    image.width = static_cast<size_t>(width);
    image.height = static_cast<size_t>(height);
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = static_cast<size_t>(width) * 4;
    image.slicePitch = image.rowPitch * static_cast<size_t>(height);
    std::vector<std::uint8_t> copy(image.slicePitch);
    std::memcpy(copy.data(), rgba, copy.size());
    image.pixels = copy.data();

    DirectX::ScratchImage source;
    HRESULT hr = source.InitializeFromImage(image);
    if (FAILED(hr))
    {
        error = "DirectXTex InitializeFromImage failed";
        return false;
    }
    DirectX::ScratchImage mips;
    hr = DirectX::GenerateMipMaps(*source.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, mips);
    if (FAILED(hr))
    {
        hr = mips.InitializeFromImage(*source.GetImage(0, 0, 0));
        if (FAILED(hr))
        {
            error = "GenerateMipMaps failed";
            return false;
        }
    }
    DXGI_FORMAT format = DXGI_FORMAT_BC3_UNORM;
    DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_DEFAULT;
    if (kind == CookedTextureKind::Normal)
        format = DXGI_FORMAT_BC5_UNORM;
    else
    {
        format = DXGI_FORMAT_BC7_UNORM;
        compressFlags = DirectX::TEX_COMPRESS_BC7_QUICK;
    }
    DirectX::ScratchImage compressed;
    HRESULT hrCompress = DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), format,
        compressFlags, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
    if (FAILED(hrCompress) && kind != CookedTextureKind::Normal)
    {
        format = DXGI_FORMAT_BC3_UNORM;
        compressFlags = DirectX::TEX_COMPRESS_DEFAULT;
        hrCompress = DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), format,
            compressFlags, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
    }
    const DirectX::Image *saveImages = mips.GetImages();
    size_t saveCount = mips.GetImageCount();
    const DirectX::TexMetadata *saveMeta = &mips.GetMetadata();
    if (SUCCEEDED(hrCompress))
    {
        saveImages = compressed.GetImages();
        saveCount = compressed.GetImageCount();
        saveMeta = &compressed.GetMetadata();
    }
    std::filesystem::create_directories(path.parent_path());
    hr = DirectX::SaveToDDSFile(saveImages, saveCount, *saveMeta, DirectX::DDS_FLAGS_NONE, wide(path).c_str());
    if (FAILED(hr))
    {
        error = "SaveToDDSFile failed";
        return false;
    }
    return true;
}

} // namespace

bool cookImageToDds(const std::filesystem::path &source, const std::filesystem::path &output, bool normalMap,
    std::string &error)
{
    int width = 0;
    int height = 0;
    int components = 0;
    const std::u8string u8 = source.u8string();
    const std::string path(u8.begin(), u8.end());
    stbi_uc *pixels = stbi_load(path.c_str(), &width, &height, &components, 4);
    if (pixels == nullptr)
    {
        error = "stb_image failed on " + path;
        return false;
    }
    const CookedTextureKind kind = normalMap ? CookedTextureKind::Normal : CookedTextureKind::Color;
    const bool ok = writeDdsFromRgba8(output, width, height, pixels, kind, error);
    stbi_image_free(pixels);
    return ok;
}
