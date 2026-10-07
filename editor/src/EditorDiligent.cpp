#include "Editor.hpp"

#include "Camera.hpp"
#include "DemoScene.hpp"
#include "EngineSettings.hpp"
#include "GfxDevice.hpp"
#include "GfxFx.hpp"
#include "GfxQuality.hpp"
#include "GfxView.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#if defined(RAYTRACER_DILIGENT)

// Use DiligentTools' bundled ImGui (matches ImGuiImplDiligent), not third_party/.
#include "imgui.h"

namespace
{

void feedImGuiFromSdl(SDL_Window *window, float dt)
{
    ImGuiIO &io = ImGui::GetIO();
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    io.DisplaySize = ImVec2(static_cast<float>(std::max(1, w)), static_cast<float>(std::max(1, h)));
    io.DeltaTime = dt > 0.f ? dt : (1.f / 60.f);

    float mx = 0;
    float my = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    io.AddMousePosEvent(mx, my);
    io.AddMouseButtonEvent(0, (buttons & SDL_BUTTON_LMASK) != 0);
    io.AddMouseButtonEvent(1, (buttons & SDL_BUTTON_RMASK) != 0);
    io.AddMouseButtonEvent(2, (buttons & SDL_BUTTON_MMASK) != 0);

    const bool *keys = SDL_GetKeyboardState(nullptr);
    io.AddKeyEvent(ImGuiKey_Escape, keys[SDL_SCANCODE_ESCAPE]);
    io.AddKeyEvent(ImGuiKey_W, keys[SDL_SCANCODE_W]);
    io.AddKeyEvent(ImGuiKey_A, keys[SDL_SCANCODE_A]);
    io.AddKeyEvent(ImGuiKey_S, keys[SDL_SCANCODE_S]);
    io.AddKeyEvent(ImGuiKey_D, keys[SDL_SCANCODE_D]);
    io.AddKeyEvent(ImGuiKey_Space, keys[SDL_SCANCODE_SPACE]);
}

} // namespace

int runEditorDiligent(int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // No SDL_WINDOW_OPENGL — Diligent owns the swapchain (D3D12 / Vulkan).
    SDL_Window *window = SDL_CreateWindow("Raytracer (Diligent)", width, height, SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    GfxDevice device;
    std::string error;
    if (!device.init(window, error))
    {
        std::cerr << "GfxDevice::init failed: " << error << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::cout << "Diligent primary view backend: " << device.backendName() << "\n";

    GfxView view;
    if (!view.init(device, error))
    {
        std::cerr << "GfxView::init failed: " << error << "\n";
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    GfxQuality quality = GfxQuality::fromSettings(engineSettings());
    GfxFx fx;
    if (!fx.init(device, quality, error))
        std::cerr << "GfxFx optional init: " << error << "\n";

    Scene scene = createDemoScene();
    Camera camera(Vec3(0, 2.2, 9), Vec3(0, 1.2, 0), Vec3(0, 1, 0), 60, 16.0 / 9.0);
    view.upload(scene, camera);
    std::cout << "Diligent view meshes=" << view.meshInstances() << " lights=" << view.lightCount() << "\n";

    bool running = true;
    bool playing = false;
    float yaw = 0.f;
    float pitch = -0.15f;
    float orbitDist = 9.f;
    const Vec3 orbitTarget(0, 1.2, 0);
    Uint64 lastTicks = SDL_GetTicks();
    float wheel = 0.f;
    bool sceneDirty = false;

    bool loggedFirst = false;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_MOUSE_WHEEL)
                wheel += event.wheel.y;
            if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                int w = 0;
                int h = 0;
                SDL_GetWindowSizeInPixels(window, &w, &h);
                device.resize(w, h);
            }
        }

        const Uint64 now = SDL_GetTicks();
        const float dt = std::min(0.05f, static_cast<float>(now - lastTicks) * 0.001f);
        lastTicks = now;

        // Orbit from raw SDL (ImGui overlay is optional; kept after first opaque frame on fragile ICDs).
        float mx = 0;
        float my = 0;
        const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
        static float prevMx = mx;
        static float prevMy = my;
        if ((buttons & SDL_BUTTON_LMASK) != 0)
        {
            yaw += (mx - prevMx) * 0.005f;
            pitch = std::clamp(pitch + (my - prevMy) * 0.005f, -1.2f, 1.2f);
        }
        prevMx = mx;
        prevMy = my;
        if (wheel != 0.f)
        {
            orbitDist = std::clamp(orbitDist - wheel * 0.6f, 2.f, 40.f);
            wheel = 0.f;
        }
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        Vec3 eye = orbitTarget + Vec3(sy * cp * orbitDist, -sp * orbitDist, cy * cp * orbitDist);
        camera = Camera(eye, orbitTarget, Vec3(0, 1, 0), 60,
            static_cast<double>(std::max(1, device.width())) / std::max(1, device.height()));

        (void)playing;
        if (sceneDirty)
        {
            view.upload(scene, camera);
            sceneDirty = false;
        }

        float sunDir[3] = {0.35f, -0.85f, 0.25f};
        if (fx.ready())
            fx.beginSky(sunDir, 12.f);

        device.beginFrame();
        device.clear(0.45f, 0.62f, 0.92f, 1.f);
        view.draw(scene, camera);
        if (fx.ready())
            fx.endPost();

        // ImGui after first opaque present (font PSO bring-up is safer once the swapchain has presented).
        if (loggedFirst)
        {
            device.imguiEnsure();
            feedImGuiFromSdl(window, dt);
            device.imguiNewFrame();
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Diligent view"))
            {
                ImGui::Text("%s | meshes %d | lights %d", device.backendName().c_str(), view.meshInstances(),
                    view.lightCount());
                if (ImGui::Button(playing ? "Stop" : "Play"))
                    playing = !playing;
                bool dirty = false;
                dirty |= ImGui::SliderFloat("View distance", &quality.viewDistance, 50.f, 2000.f);
                dirty |= ImGui::SliderInt("Shadow map", &quality.shadowMapSize, 256, 4096);
                dirty |= ImGui::SliderInt("Cascades", &quality.shadowCascades, 1, 4);
                dirty |= ImGui::SliderFloat("Particle density", &quality.particleDensity, 0.f, 2.f);
                dirty |= ImGui::Checkbox("Bloom", &quality.bloom);
                dirty |= ImGui::Checkbox("Atmospheric sky", &quality.atmosphericSky);
                if (dirty)
                {
                    quality.applyToSettings(engineSettings());
                    if (fx.ready())
                        fx.applyQuality(quality);
                    sceneDirty = true;
                }
                ImGui::TextUnformatted("Primary view is Diligent (no GL swapchain).");
            }
            ImGui::End();
            device.imguiRender();
        }
        device.endFrame();
        loggedFirst = true;
    }

    fx.shutdown();
    view.shutdown();
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#else

int runEditorDiligent(int, int)
{
    std::cerr << "Diligent editor path not enabled in this build.\n";
    return 1;
}

#endif
