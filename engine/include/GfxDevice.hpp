#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

// Diligent swapchain + device. Primary interactive view (plan Phase 2).
// Windows: Direct3D 12. Linux bring-up: Vulkan.
class GfxDevice
{
public:
    GfxDevice();
    ~GfxDevice();

    GfxDevice(const GfxDevice &) = delete;
    GfxDevice &operator=(const GfxDevice &) = delete;

    // Create device + swapchain from an SDL window (no OpenGL flag).
    bool init(SDL_Window *window, std::string &error);
    void shutdown();
    bool ready() const { return ready_; }
    const std::string &backendName() const { return backend_; }

    void resize(int width, int height);
    void beginFrame();
    // Clear color + depth for the current swapchain target.
    void clear(float r, float g, float b, float a = 1.f);
    void endFrame(); // present

    // Optional ImGui integration (DiligentTools renderer). Safe no-ops if unused.
    void imguiEnsure(); // Create ImGui context + Diligent renderer (no NewFrame).
    void imguiNewFrame();
    void imguiRender();

    int width() const { return width_; }
    int height() const { return height_; }

    // Opaque handle for GfxView / cluster / FX (Diligent IRenderDevice*).
    void *nativeDevice() const;
    void *nativeContext() const;
    void *nativeSwapChain() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    std::string backend_;
    int width_ = 0;
    int height_ = 0;
};
