#include "Editor.hpp"

#include "Constants.hpp"
#include "DemoScene.hpp"
#include "EngineSettings.hpp"
#include "GpuLimits.hpp"
#include "GpuRayTracer.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "SceneFile.hpp"
#include "Sphere.hpp"

#include "stb/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#if !defined(_WIN32)

int runEditor(int, int, int, int, bool)
{
    std::cerr << "The editor requires Windows.\n";
    return 1;
}

#else

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <GL/gl.h>

#include "Sound.hpp"
#include "DebugDraw.hpp"

#include "EditorInternal.hpp"

#include "Play.hpp"
#include "SimSave.hpp"

#include <tracy/Tracy.hpp>

namespace ed
{

SDL_Window *g_window = nullptr;
int g_windowWidth = 1280;
int g_windowHeight = 800;
ImVec2 g_viewImageMin;
ImVec2 g_viewImageMax;
bool g_viewImageShown = false;
std::string gAssetPath;
int gSavedWidth = 0;
int gSavedHeight = 0;
int gSavedSamples = 0;
int gSavedBounces = -1;
int gKeyForward = SDLK_Z;
int gKeyBack = SDLK_S;
int gKeyLeft = SDLK_Q;
int gKeyRight = SDLK_D;
int gKeyJump = SDLK_SPACE;
int gKeyJumpAlt = SDLK_E;
int gKeyUse = SDLK_F;
int gCaptureBind = 0;

bool gStopPrompt = false;
std::optional<Scene> gKeptScene;

bool keyDown(int key)
{
    if (key == 0)
        return false;
    const SDL_Scancode scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(key), nullptr);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return false;
    const bool *keys = SDL_GetKeyboardState(nullptr);
    return keys != nullptr && keys[scancode];
}

void *nativeWindowHandle()
{
    if (g_window == nullptr)
        return nullptr;
    return SDL_GetPointerProperty(
        SDL_GetWindowProperties(g_window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr);
}

bool editorWindowFocused()
{
    return g_window != nullptr && SDL_GetKeyboardFocus() == g_window;
}

void requestEditorQuit()
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

void refreshWindowSize()
{
    if (g_window == nullptr)
        return;
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);
    if (width > 0 && height > 0)
    {
        g_windowWidth = width;
        g_windowHeight = height;
    }
}

Vec3 cameraForwardXZ(const ViewState &view)
{
    Vec3 forward = view.lookAt - view.lookFrom;
    forward.y = 0;
    if (length(forward) < 1e-8)
        return Vec3(0, 0, -1);
    return normalize(forward);
}

bool pickScenePath(bool save, std::filesystem::path &path)
{
    wchar_t buffer[MAX_PATH] = {};
    if (save)
        std::wcscpy(buffer, L"scene.scene");

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(nativeWindowHandle());
    dialog.lpstrFilter = L"Scene\0*.scene\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_NOCHANGEDIR | (save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    dialog.lpstrDefExt = L"scene";
    const BOOL chosen = save ? ::GetSaveFileNameW(&dialog) : ::GetOpenFileNameW(&dialog);
    if (chosen == FALSE)
        return false;
    path = buffer;
    return true;
}

void pushEditorHistory(const Scene &scene, const ViewState &view);




EditorHistory editorHistory;

void presentLoading()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
        ImGui_ImplSDL3_ProcessEvent(&event);
    refreshWindowSize();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 center(
        viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin(
        "Loading",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("Loading...");
    ImGui::End();
    ImGui::Render();
    const int width = g_windowWidth > 0 ? g_windowWidth : 640;
    const int height = g_windowHeight > 0 ? g_windowHeight : 480;
    glViewport(0, 0, width, height);
    glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_window);
}

} // namespace ed

using namespace ed;

int runEditor(int width, int height, int samples, int depth, bool gameMode)
{
    loadEditorSettings();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::cerr << "Could not initialize SDL: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        gameMode ? "Game" : "Ray Tracer",
        1440,
        900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        std::cerr << "Could not create a window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    g_window = window;
    refreshWindowSize();

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr)
    {
        std::cerr << "Could not create an OpenGL context: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        g_window = nullptr;
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);

    float dpiScale = SDL_GetWindowDisplayScale(window);
    if (dpiScale < 0.25f)
        dpiScale = 1.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");
    presentLoading();

    CameraSetup setup = demoCameraSetup();
    ViewState view;
    view.lookFrom = setup.lookFrom;
    view.lookAt = setup.lookAt;
    view.fov = setup.fovY;
    view.aperture = setup.aperture;
    view.focusDistance = setup.focusDistance > 1e-4 ? setup.focusDistance : length(setup.lookAt - setup.lookFrom);
    view.width = std::clamp(gSavedWidth > 0 ? gSavedWidth : width, 160, 3840);
    view.height = std::clamp(gSavedHeight > 0 ? gSavedHeight : height, 120, 2160);
    view.samples = std::clamp(gSavedSamples > 0 ? gSavedSamples : samples, 1, 4);
    view.depth = std::clamp(gSavedBounces >= 0 ? gSavedBounces : depth, 0, 6);

    Scene scene = createDemoScene();
    Scene playSnapshot;
    PlayState playState;
    SimSession sim;
    CommandRecorder playRecorder;
    sim.attach(scene, playState);
    sim.setRecorder(&playRecorder);
    std::string notice;
    std::uint64_t requested = 1;
    int renderMs = 0;
    int refineCount = 0;
    int refineTarget = 0;
    bool wasPlaying = false;
    double playAccumulator = 0;
    float timeScale = 1.0f;
    ChaseCamera chase;
    Vec3 savedLookFrom = view.lookFrom;
    Vec3 savedLookAt = view.lookAt;
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 perfLast = 0;

    GpuRayTracer gpu;
    bool useGpu = gpu.init();
    if (useGpu)
        std::cout << "GPU renderer enabled\n";
    else
        std::cerr << "GPU renderer unavailable (" << gpu.failure() << ").\n";

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    std::uint64_t shownRevision = 0;
    int latestWidth = 0;
    int latestHeight = 0;
    bool done = false;
    perfLast = SDL_GetPerformanceCounter();

    while (!done)
    {
        ZoneScopedN("EditorFrame");

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && event.window.windowID == SDL_GetWindowID(window))
                refreshWindowSize();
        }
        if (done)
            break;
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0)
        {
            SDL_Delay(10);
            continue;
        }

        const Uint64 perfNow = SDL_GetPerformanceCounter();
        double frameDt = static_cast<double>(perfNow - perfLast) / static_cast<double>(perfFreq);
        perfLast = perfNow;
        frameDt = clampFrameDt(frameDt);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        bool dirty = false;
        bool saveRequested = false;
        bool savePlayRequested = false;
        bool loadPlayRequested = false;
        static double materialTime = 0;
        materialTime += frameDt;
        dirty = drawInterface(
            scene,
            view,
            static_cast<ImTextureID>(texture),
            shownRevision != 0,
            latestWidth,
            latestHeight,
            renderMs,
            refineCount,
            refineTarget,
            useGpu ? "GPU renderer" : "GPU unavailable",
            useGpu ? std::string() : gpu.failure(),
            notice,
            saveRequested,
            savePlayRequested,
            loadPlayRequested,
            gameMode);
        syncPrefabInstances(scene);
        if (refreshAssets(scene) || materialScrolling(scene))
            dirty = true;
        const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput;
        if (gameMode && !view.playing && escapePressed)
            requestEditorQuit();
        if (view.playing && escapePressed && playState.result.empty())
            view.paused = !view.paused;
        if (gameMode && !view.playing)
            drawTitleScreen(view);

        if (gStopPrompt)
            view.paused = true;
        if (gStopPrompt)
            ImGui::OpenPopup("Keep play changes");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Keep play changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Keep the scene as it is now, or restore it to the moment Play was pressed?");
            if (ImGui::Button("Keep", ImVec2(120, 0)))
            {
                sim.clearDisplay();
                gKeptScene = scene.clone();
                view.playing = false;
                gStopPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0)))
            {
                view.playing = false;
                gStopPrompt = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        updatePlayCursor(view.playing && !gStopPrompt && !view.paused && playState.result.empty() && editorWindowFocused());

        auto syncPlaySession = [&]() {
            if (view.playing == wasPlaying)
                return;
            ++requested;
            if (view.playing)
            {
                const bool paused = view.paused;
                savedLookFrom = view.lookFrom;
                savedLookAt = view.lookAt;
                playSnapshot = scene.clone();
                playAccumulator = 0;
                playState = {};
                playState.room = 1;
                view.paused = paused;
                scene.particles().clear();
                playRecorder.clear();
                sim.begin();
                armChaseCamera(chase, view);
                startMusic();
            }
            else
            {
                if (gKeptScene.has_value())
                {
                    scene = std::move(*gKeptScene);
                    gKeptScene.reset();
                }
                else
                    scene = std::move(playSnapshot);
                scene.particles().clear();
                view.lookFrom = savedLookFrom;
                view.lookAt = savedLookAt;
                playState = {};
                chase = {};
                stopMusic();
                updatePlayCursor(false);
            }
            wasPlaying = view.playing;
        };
        syncPlaySession();

        bool stepOnce = false;
        if (view.playing && !gameMode && !view.paused)
        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f, viewport->WorkPos.y + 8.0f),
                ImGuiCond_Always,
                ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::Begin(
                "PlaySpeed",
                nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Speed", &timeScale, 0.25f, 2.0f, "%.2fx");
            ImGui::End();
        }
        bool restart = false;
        if (drawPauseMenu(view, playState, gameMode, timeScale, stepOnce, restart))
        {
            if (gameMode)
                view.playing = false;
            else
                gStopPrompt = true;
        }
        if (restart)
        {
            sim.clearDisplay();
            scene = playSnapshot.clone();
            playState = {};
            playState.room = 1;
            playAccumulator = 0;
            scene.particles().clear();
            playRecorder.clear();
            sim.begin();
            armChaseCamera(chase, view);
            placeChaseCamera(scene, view, chase, playState);
            ++requested;
        }
        syncPlaySession();

        if (view.playing && (savePlayRequested || loadPlayRequested))
        {
            sim.clearDisplay();
            const std::filesystem::path path = defaultPlaySessionPath();
            if (savePlayRequested)
            {
                if (savePlaySession(path, scene, playState, sim.tick(), sim.lastLook(), playRecorder.commands()))
                    notice = "Saved " + path.filename().string();
                else
                    notice = "Could not save play session";
            }
            if (loadPlayRequested)
            {
                PlaySessionInfo info;
                if (loadPlaySession(path, scene, playState, info))
                {
                    playRecorder.clear();
                    for (const Command &command : info.commands)
                        playRecorder.record(command);
                    sim.primeFromCurrent(info.tick, info.lastLook);
                    applyPlayCamera(scene, view, chase, playState);
                    notice = "Loaded " + path.filename().string();
                }
                else
                    notice = "Could not load play session";
            }
            ++requested;
        }

        bool cameraToggled = false;
        if (view.playing)
        {
            const bool cDown = keyDown(SDLK_C);
            const bool allowC = !view.paused && playState.result.empty()
                && !ImGui::GetIO().WantTextInput && editorWindowFocused();
            if (allowC && cDown && !chase.cWasDown && chase.shot < 0)
            {
                chase.firstPerson = !chase.firstPerson;
                cameraToggled = true;
            }
            chase.cWasDown = cDown;
            const bool vDown = keyDown(SDLK_V);
            if (allowC && vDown && !chase.vWasDown && !scene.shots().empty())
            {
                chase.blendFrom = view.lookFrom;
                chase.blendAt = view.lookAt;
                chase.blendFov = view.fov;
                chase.shot += 1;
                if (chase.shot >= static_cast<int>(scene.shots().size()))
                    chase.shot = -1;
                chase.blending = true;
                chase.blend = 0;
                cameraToggled = true;
            }
            chase.vWasDown = vDown;
        }

        if (view.playing && (!view.paused || stepOnce))
        {
            const bool textIdle = !ImGui::GetIO().WantTextInput;
            const bool lookChanged = textIdle && !view.paused && chase.shot < 0 && !chase.blending && updatePlayLook(chase);
            if (chase.blending)
                chase.blend = std::min(1.0, chase.blend + frameDt / 0.35);
            if (lookChanged || cameraToggled || chase.blending)
                applyPlayCamera(scene, view, chase, playState);
            const bool readKeys = textIdle && editorWindowFocused();
            const int steps = takePlaySteps(frameDt, timeScale, playAccumulator, stepOnce);
            const bool resumeAfter = stepOnce && view.paused;
            if (resumeAfter)
                view.paused = false;
            Vec3 look = view.lookAt - view.lookFrom;
            if (length(look) < 1e-8)
                look = Vec3(0, 0, -1);
            else
                look = normalize(look);
            for (int step = 0; step < steps; ++step)
            {
                PlayInput input;
                if (readKeys)
                    input = readPlayInput();
                sim.enqueue(commandFromPlayInput(input, sim.nextTick() + static_cast<SimTick>(step), look));
            }
            sim.take(steps);
            const double alpha = (stepOnce || view.paused) ? 1.0 : playAccumulator / kPlayStep;
            sim.applyDisplay(alpha);
            if (view.renderStream)
            {
                double focusX = view.lookFrom.x;
                double focusZ = view.lookFrom.z;
                if (playState.playerId != kInvalidEntityId)
                {
                    if (Object *player = scene.find(playState.playerId))
                    {
                        const Vec3 at = player->displayWorldPosition();
                        focusX = at.x;
                        focusZ = at.z;
                    }
                }
                view.renderStream->setFocus(focusX, focusZ);
                view.renderStream->pump(scene);
            }
            applyPlayCamera(scene, view, chase, playState);
            playSimEvents(scene, sim.events(), view.lookFrom);
            if (resumeAfter && playState.result.empty())
                view.paused = true;
            sim.refreshHud();
            if (steps > 0 || lookChanged || cameraToggled || chase.blending || !view.paused)
                ++requested;
            if (chase.blend >= 1.0)
                chase.blending = false;
        }
        else if (view.playing)
            sim.refreshHud();

        if (view.playing)
        {
            scene.advanceParticles(frameDt);
            if (!scene.particles().empty())
                ++requested;
        }

        const char *cameraLabel = "";
        if (chase.shot >= 0 && chase.shot < static_cast<int>(scene.shots().size()))
            cameraLabel = scene.shots()[static_cast<size_t>(chase.shot)].name.c_str();
        drawPlayHud(sim.snapshot(), view, timeScale, chase.firstPerson, cameraLabel);
        if (dirty)
            ++requested;

        const bool preview = useGpu && requested != shownRevision;
        const bool refine = useGpu && !view.playing && !preview && refineTarget > 1 && refineCount < refineTarget;
        if (preview || refine)
        {
            std::uint64_t want = requested;
            Camera camera = viewCamera(view, static_cast<double>(view.width) / static_cast<double>(view.height));
            int ms = -1;
            if (preview)
            {
                ms = gpu.render(
                    scene,
                    camera,
                    texture,
                    view.width,
                    view.height,
                    1,
                    view.depth,
                    view.selectedObject,
                    false,
                    -1,
                    materialTime,
                    engineSettings().mirrorBounces);
            }
            else
            {
                ms = gpu.render(
                    scene,
                    camera,
                    texture,
                    view.width,
                    view.height,
                    view.samples,
                    view.depth,
                    view.selectedObject,
                    false,
                    refineCount,
                    materialTime,
                    engineSettings().mirrorBounces);
            }
            if (ms >= 0)
            {
                renderMs = ms;
                latestWidth = view.width;
                latestHeight = view.height;
                if (preview)
                {
                    shownRevision = want;
                    refineCount = 0;
                    refineTarget = view.playing || view.samples <= 1 ? 0 : view.samples * view.samples;
                }
                else
                    ++refineCount;
            }
        }

        if (saveRequested)
        {
            std::vector<std::uint8_t> pixels;
            if (useGpu)
                gpu.readPixels(texture, latestWidth, latestHeight, pixels);
            if (!pixels.empty() && stbi_write_png("render.png", latestWidth, latestHeight, 4, pixels.data(), latestWidth * 4) != 0)
            {
                notice = "Saved render.png";
                std::cout << "Saved render.png\n";
            }
            else
            {
                notice = "Could not write render.png";
                std::cerr << "Could not write render.png\n";
            }
        }

        ImGui::Render();
        glViewport(0, 0, g_windowWidth, g_windowHeight);
        glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        FrameMark;
    }

    gpu.shutdown();
    shutdownPlayInput();
    updatePlayCursor(false);

    glDeleteTextures(1, &texture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    g_window = nullptr;
    SDL_Quit();
    return 0;
}

#endif
