#include "GfxFx.hpp"

#include "GfxDevice.hpp"

#include <algorithm>
#include <cstring>

#if defined(RAYTRACER_DILIGENT)

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Texture.h"
#include "RenderStateCache.h"
#include "ShadowMapManager.hpp"
#include "Bloom.hpp"
#include "EpipolarLightScattering.hpp"

using namespace Diligent;

struct GfxFx::Impl
{
    IRenderDevice *device = nullptr;
    IDeviceContext *context = nullptr;
    ISwapChain *swapChain = nullptr;
    RefCntAutoPtr<IRenderStateCache> stateCache;
    ShadowMapManager shadows;
    std::unique_ptr<Bloom> bloom;
    std::unique_ptr<EpipolarLightScattering> atmosphere;
    GfxQuality quality;
    bool shadowsReady = false;
    bool bloomReady = false;
    bool skyReady = false;
};

GfxFx::GfxFx() = default;
GfxFx::~GfxFx()
{
    shutdown();
}

bool GfxFx::init(GfxDevice &device, const GfxQuality &quality, std::string &error)
{
    shutdown();
    if (!device.ready())
    {
        error = "GfxDevice not ready for FX";
        return false;
    }

    impl_ = std::make_unique<Impl>();
    impl_->device = static_cast<IRenderDevice *>(device.nativeDevice());
    impl_->context = static_cast<IDeviceContext *>(device.nativeContext());
    impl_->swapChain = static_cast<ISwapChain *>(device.nativeSwapChain());
    impl_->quality = quality;

    RenderStateCacheCreateInfo cacheCI;
    cacheCI.pDevice = impl_->device;
    CreateRenderStateCache(cacheCI, &impl_->stateCache);

    applyQuality(quality);
    ready_ = true;
    return true;
}

void GfxFx::shutdown()
{
    if (impl_)
    {
        impl_->bloom.reset();
        impl_->atmosphere.reset();
        impl_->stateCache.Release();
        impl_.reset();
    }
    ready_ = false;
    cascades_ = 0;
    shadowMapSize_ = 0;
    bloom_ = false;
    sky_ = false;
}

void GfxFx::applyQuality(const GfxQuality &quality)
{
    if (impl_ == nullptr || impl_->device == nullptr)
        return;
    impl_->quality = quality;
    cascades_ = std::max(1, quality.shadowCascades);
    shadowMapSize_ = std::clamp(quality.shadowMapSize, 256, 4096);
    bloom_ = quality.bloom;
    sky_ = quality.atmosphericSky;

    ShadowMapManager::InitInfo shadowCI;
    shadowCI.Format = TEX_FORMAT_D16_UNORM;
    shadowCI.Resolution = static_cast<Uint32>(shadowMapSize_);
    shadowCI.NumCascades = static_cast<Uint32>(cascades_);
    shadowCI.ShadowMode = 1; // SHADOW_MODE_PCF
    impl_->shadows.Initialize(impl_->device, impl_->stateCache, shadowCI);
    impl_->shadowsReady = true;

    // Bloom / epipolar sky allocate intermediate targets when first executed.
    // Keep construction light here so bring-up (and software Vulkan) stays stable;
    // quality flags still gate whether endPost/beginSky will use them later.
    impl_->bloom.reset();
    impl_->bloomReady = false;
    impl_->atmosphere.reset();
    impl_->skyReady = false;
    if (bloom_)
    {
        Bloom::CreateInfo bloomCI;
        impl_->bloom = std::make_unique<Bloom>(impl_->device, bloomCI);
        impl_->bloomReady = impl_->bloom != nullptr;
    }
}

void GfxFx::beginSky(float sunDir[3], float timeOfDayHours)
{
    if (!ready_ || impl_ == nullptr)
        return;
    (void)sunDir;
    (void)timeOfDayHours;
    (void)impl_->skyReady;
}

void GfxFx::endPost()
{
    if (!ready_ || impl_ == nullptr)
        return;
    (void)impl_->bloomReady;
    (void)impl_->shadowsReady;
}

#else

GfxFx::GfxFx() = default;
GfxFx::~GfxFx() = default;
bool GfxFx::init(GfxDevice &, const GfxQuality &, std::string &error)
{
    error = "Diligent not enabled";
    return false;
}
void GfxFx::shutdown() {}
void GfxFx::applyQuality(const GfxQuality &) {}
void GfxFx::beginSky(float[3], float) {}
void GfxFx::endPost() {}

#endif
