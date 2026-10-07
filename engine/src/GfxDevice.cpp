#include "GfxDevice.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#if defined(RAYTRACER_DILIGENT)

#include "RefCntAutoPtr.hpp"
#include "DeviceContext.h"
#include "EngineFactory.h"
#include "RenderDevice.h"
#include "SwapChain.h"
#include "ImGuiDiligentRenderer.hpp"
#include "ImGuiImplDiligent.hpp"

#if defined(RAYTRACER_DILIGENT_D3D12)
#    include "EngineFactoryD3D12.h"
#elif defined(RAYTRACER_DILIGENT_VULKAN)
#    include "EngineFactoryVk.h"
#endif

using namespace Diligent;

struct GfxDevice::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ISwapChain> swapChain;
    RefCntAutoPtr<IEngineFactory> factory;
    std::unique_ptr<ImGuiImplDiligent> imgui;
    bool imguiOwnedContext = false;
};

GfxDevice::GfxDevice() = default;
GfxDevice::~GfxDevice()
{
    shutdown();
}

void *GfxDevice::nativeDevice() const
{
    return impl_ ? impl_->device.RawPtr() : nullptr;
}

void *GfxDevice::nativeContext() const
{
    return impl_ ? impl_->context.RawPtr() : nullptr;
}

void *GfxDevice::nativeSwapChain() const
{
    return impl_ ? impl_->swapChain.RawPtr() : nullptr;
}

bool GfxDevice::init(SDL_Window *window, std::string &error)
{
    shutdown();
    if (window == nullptr)
    {
        error = "null SDL window";
        return false;
    }

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w < 1 || h < 1)
    {
        error = "invalid drawable size";
        return false;
    }

    impl_ = std::make_unique<Impl>();
    SwapChainDesc SCDesc;
    SCDesc.Width = static_cast<Uint32>(w);
    SCDesc.Height = static_cast<Uint32>(h);
    SCDesc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
    SCDesc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

#if defined(RAYTRACER_DILIGENT_D3D12)
    backend_ = "D3D12";
    IEngineFactoryD3D12 *factoryD3D12 = GetEngineFactoryD3D12();
    impl_->factory = factoryD3D12;
    factoryD3D12->LoadD3D12();

    EngineD3D12CreateInfo engineCI;
    factoryD3D12->CreateDeviceAndContextsD3D12(engineCI, &impl_->device, &impl_->context);

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND, nullptr));
    if (hwnd == nullptr)
    {
        error = "SDL window has no Win32 HWND";
        shutdown();
        return false;
    }
    Win32NativeWindow nativeWindow{hwnd};
    factoryD3D12->CreateSwapChainD3D12(impl_->device, impl_->context, SCDesc, FullScreenModeDesc{}, nativeWindow,
        &impl_->swapChain);

#elif defined(RAYTRACER_DILIGENT_VULKAN)
    backend_ = "Vulkan";
    IEngineFactoryVk *factoryVk = GetEngineFactoryVk();
    impl_->factory = factoryVk;

    EngineVkCreateInfo engineCI;
    factoryVk->CreateDeviceAndContextsVk(engineCI, &impl_->device, &impl_->context);

    NativeWindow nativeWindow;
#    if defined(SDL_PLATFORM_LINUX) || defined(__linux__)
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    nativeWindow.WindowId = static_cast<Uint32>(
        SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    nativeWindow.pDisplay =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    if (nativeWindow.WindowId == 0 || nativeWindow.pDisplay == nullptr)
    {
        error = "SDL window has no X11 handle (need VIDEO_X11)";
        shutdown();
        return false;
    }
#    elif defined(_WIN32)
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    nativeWindow.hWnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND, nullptr);
#    endif
    factoryVk->CreateSwapChainVk(impl_->device, impl_->context, SCDesc, nativeWindow, &impl_->swapChain);
#else
    error = "no Diligent backend configured";
    shutdown();
    return false;
#endif

    if (!impl_->device || !impl_->context || !impl_->swapChain)
    {
        error = "Diligent device/swapchain creation failed";
        shutdown();
        return false;
    }

    // ImGui is created lazily on first imguiNewFrame() so device bring-up
    // stays independent of font/PSO construction.
    width_ = w;
    height_ = h;
    ready_ = true;
    return true;
}

void GfxDevice::shutdown()
{
    if (impl_)
    {
        if (impl_->context)
            impl_->context->Flush();
        impl_->imgui.reset();
        impl_->swapChain.Release();
        impl_->context.Release();
        impl_->device.Release();
        impl_->factory.Release();
        impl_.reset();
    }
    ready_ = false;
    backend_.clear();
    width_ = 0;
    height_ = 0;
}

void GfxDevice::resize(int width, int height)
{
    if (!ready_ || impl_ == nullptr || width < 1 || height < 1)
        return;
    if (width == width_ && height == height_)
        return;
    impl_->swapChain->Resize(static_cast<Uint32>(width), static_cast<Uint32>(height));
    width_ = width;
    height_ = height;
}

void GfxDevice::beginFrame()
{
    if (!ready_)
        return;
    auto *pRTV = impl_->swapChain->GetCurrentBackBufferRTV();
    auto *pDSV = impl_->swapChain->GetDepthBufferDSV();
    impl_->context->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GfxDevice::clear(float r, float g, float b, float a)
{
    if (!ready_)
        return;
    auto *pRTV = impl_->swapChain->GetCurrentBackBufferRTV();
    auto *pDSV = impl_->swapChain->GetDepthBufferDSV();
    const float clearColor[] = {r, g, b, a};
    impl_->context->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    impl_->context->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GfxDevice::endFrame()
{
    if (!ready_)
        return;
    impl_->swapChain->Present(1);
}

void GfxDevice::imguiEnsure()
{
    if (!ready_ || impl_ == nullptr || impl_->imgui)
        return;
    const auto &sc = impl_->swapChain->GetDesc();
    ImGuiDiligentCreateInfo ci(impl_->device, sc.ColorBufferFormat, sc.DepthBufferFormat);
    impl_->imgui = std::make_unique<ImGuiImplDiligent>(ci);
    impl_->imguiOwnedContext = true;
}

void GfxDevice::imguiNewFrame()
{
    if (!ready_ || impl_ == nullptr)
        return;
    imguiEnsure();
    if (!impl_->imgui)
        return;
    const auto &sc = impl_->swapChain->GetDesc();
    impl_->imgui->NewFrame(sc.Width, sc.Height, sc.PreTransform);
}

void GfxDevice::imguiRender()
{
    if (!ready_ || impl_ == nullptr || !impl_->imgui)
        return;
    impl_->imgui->Render(impl_->context);
}

#else // !RAYTRACER_DILIGENT

GfxDevice::GfxDevice() = default;
GfxDevice::~GfxDevice() = default;
bool GfxDevice::init(SDL_Window *, std::string &error)
{
    error = "Diligent not enabled in this build";
    return false;
}
void GfxDevice::shutdown() {}
void GfxDevice::resize(int, int) {}
void GfxDevice::beginFrame() {}
void GfxDevice::clear(float, float, float, float) {}
void GfxDevice::endFrame() {}
void GfxDevice::imguiEnsure() {}
void GfxDevice::imguiNewFrame() {}
void GfxDevice::imguiRender() {}
void *GfxDevice::nativeDevice() const { return nullptr; }
void *GfxDevice::nativeContext() const { return nullptr; }
void *GfxDevice::nativeSwapChain() const { return nullptr; }

#endif
