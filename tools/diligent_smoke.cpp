#include "GfxDevice.hpp"
#include "GfxView.hpp"
#include "DemoScene.hpp"
#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <string>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Diligent smoke", 1280, 720, SDL_WINDOW_RESIZABLE);
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
    std::cout << "Diligent backend: " << device.backendName() << "\n";

    GfxView view;
    if (!view.init(device, error))
    {
        std::cerr << "GfxView::init failed: " << error << "\n";
        device.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Scene scene = createDemoScene();
    Camera camera(Vec3(0, 2, 8), Vec3(0, 1, 0), Vec3(0, 1, 0), 60, 16.0 / 9.0);
    view.upload(scene, camera);

    // A few frames of clear + draw + present.
    for (int i = 0; i < 3; ++i)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        device.resize(w, h);
        device.beginFrame();
        device.clear(0.05f, 0.07f, 0.10f, 1.f);
        view.draw(scene, camera);
        device.endFrame();
    }

    std::cout << "smoke ok meshes=" << view.meshInstances() << " lights=" << view.lightCount() << "\n";
    view.shutdown();
    device.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
