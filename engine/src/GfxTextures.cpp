#include "GfxDevice.hpp"
#include "ImageIO.hpp"

#include <string>
#include <vector>

#if defined(RAYTRACER_DILIGENT)

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "Texture.h"

using namespace Diligent;

// Create an RGBA8 Diligent texture from a loaded image (BC upload path when mips/compressed land).
RefCntAutoPtr<ITexture> gfxCreateTexture2D(IRenderDevice *device, const LoadedImage &image, const char *name)
{
    RefCntAutoPtr<ITexture> texture;
    if (device == nullptr || image.width < 1 || image.height < 1 || image.rgba8.empty())
        return texture;

    TextureDesc desc;
    desc.Name = name != nullptr ? name : "GfxTexture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Uint32>(image.width);
    desc.Height = static_cast<Uint32>(image.height);
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;

    TextureData data;
    TextureSubResData sub;
    sub.pData = image.rgba8.data();
    sub.Stride = static_cast<Uint64>(image.width) * 4u;
    data.pSubResources = &sub;
    data.NumSubresources = 1;
    device->CreateTexture(desc, &data, &texture);
    return texture;
}

#else

void gfxTexturesStub() {}

#endif
