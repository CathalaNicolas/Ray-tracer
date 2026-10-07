#include "RayTracer.hpp"

#include "GpuRayTracer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)

Vec3 RayTracer::trace(const Ray &, const Scene &, int) const
{
    std::cerr << "Rendering requires the GPU shader, which is built for Windows\n";
    return {};
}

void RayTracer::render(const Scene &, const Camera &, Image &) const
{
    std::cerr << "Rendering requires the GPU shader, which is built for Windows\n";
}

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

namespace
{

struct GpuHost
{
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    GLuint texture = 0;
    GpuRayTracer gpu;
    bool ready = false;
    std::string failure;
};

GpuHost &gpuHost()
{
    static GpuHost host;
    return host;
}

bool createOffscreenContext(GpuHost &host)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = ::DefWindowProcW;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RayTracerOffscreen";
    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        host.failure = "Could not register the offscreen window class";
        return false;
    }

    host.hwnd = ::CreateWindowW(
        windowClass.lpszClassName,
        L"Ray Tracer",
        WS_POPUP,
        -32000,
        -32000,
        32,
        32,
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr);
    if (host.hwnd == nullptr)
    {
        host.failure = "Could not create an offscreen window";
        return false;
    }

    host.dc = ::GetDC(host.hwnd);
    PIXELFORMATDESCRIPTOR format = {};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    const int chosen = ::ChoosePixelFormat(host.dc, &format);
    if (chosen == 0 || ::SetPixelFormat(host.dc, chosen, &format) == FALSE)
    {
        host.failure = "Could not choose an OpenGL pixel format";
        return false;
    }

    HGLRC temporary = wglCreateContext(host.dc);
    if (temporary == nullptr || !wglMakeCurrent(host.dc, temporary))
    {
        if (temporary != nullptr)
            wglDeleteContext(temporary);
        host.failure = "Could not create an OpenGL context";
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(0x821B, &major);
    glGetIntegerv(0x821C, &minor);
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (major == 0 && minor == 0 && version != nullptr)
        std::sscanf(version, "%d.%d", &major, &minor);

    host.context = temporary;
    if (major * 100 + minor * 10 >= 300)
        return true;

    using CreateAttribs = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
    auto createAttribs = reinterpret_cast<CreateAttribs>(wglGetProcAddress("wglCreateContextAttribsARB"));
    const int attribs[] = {0x2091, 3, 0x2092, 0, 0x9126, 0x0001, 0};
    HGLRC modern = createAttribs != nullptr ? createAttribs(host.dc, nullptr, attribs) : nullptr;
    if (modern == nullptr)
        return true;

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temporary);
    host.context = modern;
    wglMakeCurrent(host.dc, host.context);
    return true;
}

bool ensureGpu()
{
    GpuHost &host = gpuHost();
    if (host.ready)
        return true;
    if (!host.failure.empty())
        return false;
    if (!createOffscreenContext(host) || !host.gpu.init())
    {
        if (host.failure.empty())
            host.failure = host.gpu.failure();
        std::cerr << host.failure << "\n";
        return false;
    }

    glGenTextures(1, &host.texture);
    glBindTexture(GL_TEXTURE_2D, host.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    host.ready = true;
    return true;
}

double encodedToLinear(std::uint8_t channel)
{
    return std::pow(static_cast<double>(channel) / 255.0, 2.2);
}

} // namespace

Vec3 RayTracer::trace(const Ray &ray, const Scene &scene, int depth) const
{
    if (!ensureGpu())
        return {};

    GpuHost &host = gpuHost();
    Vec3 direction = ray.direction;
    if (length(direction) <= 0)
        direction = Vec3(0, 0, -1);
    Camera camera(ray.origin, ray.origin + normalize(direction), Vec3(0, 1, 0), 40, 1);
    if (host.gpu.render(scene, camera, host.texture, 1, 1, 1, depth, selectedObject, true) < 0)
        return {};

    std::vector<float> pixels;
    if (!host.gpu.readPixelsLinear(host.texture, 1, 1, pixels) || pixels.size() < 3)
        return {};
    return Vec3(pixels[0], pixels[1], pixels[2]);
}

void RayTracer::render(const Scene &scene, const Camera &camera, Image &image) const
{
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0 || !ensureGpu())
        return;

    GpuHost &host = gpuHost();
    if (host.gpu.render(scene, camera, host.texture, width, height, sampleGrid, maxDepth, selectedObject, false) < 0)
        return;

    std::vector<std::uint8_t> pixels;
    if (!host.gpu.readPixels(host.texture, width, height, pixels))
        return;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
            image.set(
                x,
                y,
                Vec3(
                    encodedToLinear(pixels[index]),
                    encodedToLinear(pixels[index + 1]),
                    encodedToLinear(pixels[index + 2])));
        }
    }
}

#endif
