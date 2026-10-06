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
#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <GL/gl.h>

#pragma comment(lib, "winmm.lib")

#include "Sound.hpp"
#include "DebugDraw.hpp"

#include "EditorInternal.hpp"

#include "Play.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ed
{



HGLRC g_glContext = nullptr;
HWND g_hwnd = nullptr;
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
int gKeyForward = 'Z';
int gKeyBack = 'S';
int gKeyLeft = 'Q';
int gKeyRight = 'D';
int gKeyJump = VK_SPACE;
int gKeyJumpAlt = 'E';
int gKeyUse = 'F';
int gCaptureBind = 0;

bool gStopPrompt = false;
std::optional<Scene> gKeptScene;

bool keyDown(int key)
{
    return (::GetAsyncKeyState(key) & 0x8000) != 0;
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
    dialog.hwndOwner = g_hwnd;
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

bool createGlContext(HWND hwnd, WglWindow &window)
{
    HDC dc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR format = {};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    const int chosen = ::ChoosePixelFormat(dc, &format);
    if (chosen == 0 || ::SetPixelFormat(dc, chosen, &format) == FALSE)
    {
        ::ReleaseDC(hwnd, dc);
        return false;
    }
    ::ReleaseDC(hwnd, dc);
    window.dc = ::GetDC(hwnd);

    HGLRC temporary = wglCreateContext(window.dc);
    if (temporary == nullptr || !wglMakeCurrent(window.dc, temporary))
    {
        if (temporary != nullptr)
            wglDeleteContext(temporary);
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(0x821B, &major);
    glGetIntegerv(0x821C, &minor);
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    if (major == 0 && minor == 0 && version != nullptr)
    {
        int parsedMajor = 0;
        int parsedMinor = 0;
        if (std::sscanf(version, "%d.%d", &parsedMajor, &parsedMinor) >= 1)
        {
            major = parsedMajor;
            minor = parsedMinor;
        }
    }

    g_glContext = temporary;
    if (major * 100 + minor * 10 >= 300)
        return true;

    using CreateAttribs = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
    auto createAttribs = reinterpret_cast<CreateAttribs>(wglGetProcAddress("wglCreateContextAttribsARB"));
    const int attribs[] = {
        0x2091, 3,
        0x2092, 0,
        0x9126, 0x0001,
        0};
    HGLRC modern = createAttribs != nullptr ? createAttribs(window.dc, nullptr, attribs) : nullptr;
    if (modern != nullptr)
    {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(temporary);
        g_glContext = modern;
        wglMakeCurrent(window.dc, g_glContext);
    }
    return true;
}

LRESULT WINAPI windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
        return 1;
    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_windowWidth = LOWORD(lParam);
            g_windowHeight = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

void presentLoading(HDC dc)
{
    MSG message;
    while (::PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&message);
        ::DispatchMessage(&message);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
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
    ::SwapBuffers(dc);
}

} // namespace ed

using namespace ed;

int runEditor(int width, int height, int samples, int depth, bool gameMode)
{
    loadEditorSettings();
    ImGui_ImplWin32_EnableDpiAwareness();
    float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RayTracerEditor";
    ::RegisterClassExW(&windowClass);

    HWND hwnd = ::CreateWindowW(
        windowClass.lpszClassName,
        gameMode ? L"Game" : L"Ray Tracer",
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        static_cast<int>(1440 * dpiScale),
        static_cast<int>(900 * dpiScale),
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr);
    g_hwnd = hwnd;

    WglWindow glWindow;
    if (!createGlContext(hwnd, glWindow))
    {
        std::cerr << "Could not create an OpenGL context\n";
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        return 1;
    }
    wglMakeCurrent(glWindow.dc, g_glContext);
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();
    presentLoading(glWindow.dc);

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
    std::string notice;
    std::uint64_t requested = 1;
    int renderMs = 0;
    int refineCount = 0;
    int refineTarget = 0;
    bool wasPlaying = false;
    double playAccumulator = 0;
    float timeScale = 1.0f;
    PlayState playState;
    ChaseCamera chase;
    Vec3 savedLookFrom = view.lookFrom;
    Vec3 savedLookAt = view.lookAt;
    LARGE_INTEGER perfFreq;
    LARGE_INTEGER perfLast;
    ::QueryPerformanceFrequency(&perfFreq);

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
    ::QueryPerformanceCounter(&perfLast);

    while (!done)
    {
        MSG message;
        while (::PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&message);
            ::DispatchMessage(&message);
            if (message.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        if (::IsIconic(hwnd))
        {
            ::Sleep(10);
            continue;
        }

        LARGE_INTEGER perfNow;
        ::QueryPerformanceCounter(&perfNow);
        double frameDt = static_cast<double>(perfNow.QuadPart - perfLast.QuadPart) / static_cast<double>(perfFreq.QuadPart);
        perfLast = perfNow;
        frameDt = clampFrameDt(frameDt);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        bool dirty = false;
        bool saveRequested = false;
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
            gameMode);
        syncPrefabInstances(scene);
        if (refreshAssets(scene) || materialScrolling(scene))
            dirty = true;
        const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput;
        if (gameMode && !view.playing && escapePressed)
            ::PostQuitMessage(0);
        if (view.playing && escapePressed && playState.result.empty())
            playState.paused = !playState.paused;
        if (gameMode && !view.playing)
            drawTitleScreen(view);

        if (gStopPrompt)
            playState.paused = true;
        if (gStopPrompt)
            ImGui::OpenPopup("Keep play changes");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Keep play changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Keep the scene as it is now, or restore it to the moment Play was pressed?");
            if (ImGui::Button("Keep", ImVec2(120, 0)))
            {
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
        updatePlayCursor(view.playing && !gStopPrompt && !playState.paused && playState.result.empty() && ::GetForegroundWindow() == g_hwnd);

        auto syncPlaySession = [&]() {
            if (view.playing == wasPlaying)
                return;
            ++requested;
            if (view.playing)
            {
                const bool paused = playState.paused;
                savedLookFrom = view.lookFrom;
                savedLookAt = view.lookAt;
                playSnapshot = scene.clone();
                playAccumulator = 0;
                playState = {};
                playState.room = 1;
                playState.paused = paused;
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
        if (view.playing && !gameMode && !playState.paused)
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
            scene = playSnapshot.clone();
            playState = {};
            playState.room = 1;
            playAccumulator = 0;
            armChaseCamera(chase, view);
            for (const auto &object : scene.objects())
            {
                if (object->tag == "player")
                {
                    playState.playerId = object->id;
                    break;
                }
            }
            placeChaseCamera(scene, view, chase, playState);
            ++requested;
        }
        syncPlaySession();

        bool cameraToggled = false;
        if (view.playing)
        {
            const bool cDown = keyDown('C');
            const bool allowC = !playState.paused && playState.result.empty()
                && !ImGui::GetIO().WantTextInput && ::GetForegroundWindow() == g_hwnd;
            if (allowC && cDown && !chase.cWasDown && chase.shot < 0)
            {
                chase.firstPerson = !chase.firstPerson;
                cameraToggled = true;
            }
            chase.cWasDown = cDown;
            const bool vDown = keyDown('V');
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

        if (view.playing && (!playState.paused || stepOnce))
        {
            const bool textIdle = !ImGui::GetIO().WantTextInput;
            const bool lookChanged = textIdle && !playState.paused && chase.shot < 0 && !chase.blending && updatePlayLook(chase);
            if (chase.blending)
                chase.blend = std::min(1.0, chase.blend + frameDt / 0.35);
            if (lookChanged || cameraToggled || chase.blending)
                applyPlayCamera(scene, view, chase, playState);
            const bool readKeys = textIdle && ::GetForegroundWindow() == g_hwnd;
            const int steps = takePlaySteps(frameDt, timeScale, playAccumulator, stepOnce);
            const bool resumeAfter = stepOnce && playState.paused;
            if (resumeAfter)
                playState.paused = false;
            bool stepped = false;
            for (int step = 0; step < steps; ++step)
            {
                PlayInput input;
                if (readKeys)
                    input = readPlayInput();
                const int scoreBefore = playState.score;
                const std::string messageBefore = playState.message;
                const std::string resultBefore = playState.result;
                Vec3 look = view.lookAt - view.lookFrom;
                if (length(look) < 1e-8)
                    look = Vec3(0, 0, -1);
                else
                    look = normalize(look);
                stepPlay(scene, input, look, static_cast<float>(kPlayStep), playState);
                applyPlayCamera(scene, view, chase, playState);
                const bool scoreUp = playState.score > scoreBefore;
                const bool messageChanged = playState.message != messageBefore && !playState.message.empty();
                const bool becameWon = playState.result == "won" && resultBefore != "won";
                if (scoreUp || messageChanged || becameWon)
                    playScoreBeep(scoreUp, playState.message, becameWon, view.lookFrom, playState.eventAt);
                stepped = true;
            }
            if (resumeAfter && playState.result.empty())
                playState.paused = true;
            if (stepped || lookChanged || cameraToggled || chase.blending)
                ++requested;
            if (chase.blend >= 1.0)
                chase.blending = false;
        }

        const char *cameraLabel = "";
        if (chase.shot >= 0 && chase.shot < static_cast<int>(scene.shots().size()))
            cameraLabel = scene.shots()[static_cast<size_t>(chase.shot)].name.c_str();
        drawPlayHud(scene, view, playState, timeScale, chase.firstPerson, cameraLabel);
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
        ::SwapBuffers(glWindow.dc);
    }

    gpu.shutdown();

    glDeleteTextures(1, &texture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    wglMakeCurrent(nullptr, nullptr);
    ::ReleaseDC(hwnd, glWindow.dc);
    wglDeleteContext(g_glContext);
    g_glContext = nullptr;
    g_hwnd = nullptr;
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}

#endif
