#include "EditorInternal.hpp"

#include "DebugDraw.hpp"
#include "Sound.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ed
{
namespace
{

SDL_Gamepad *g_gamepad = nullptr;

SDL_Gamepad *playGamepad()
{
    if (g_gamepad != nullptr && SDL_GamepadConnected(g_gamepad))
        return g_gamepad;
    if (g_gamepad != nullptr)
    {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = nullptr;
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids != nullptr && count > 0)
        g_gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
    return g_gamepad;
}

float gamepadAxis(SDL_Gamepad *pad, SDL_GamepadAxis axis, float deadzone)
{
    const float value = static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0f;
    if (value > -deadzone && value < deadzone)
        return 0.0f;
    return value;
}

void burstParticles(Scene &scene, const Vec3 &at, const Vec3 &color)
{
    for (int index = 0; index < 8; ++index)
    {
        const double angle = index * 0.78539816339;
        Particle particle;
        particle.position = at;
        particle.velocity = Vec3(std::cos(angle), 1.4, std::sin(angle)) * 1.6;
        particle.color = color;
        particle.life = 0.45;
        particle.size = 0.07;
        scene.addParticle(particle);
    }
}

} // namespace

void shutdownPlayInput()
{
    if (g_gamepad != nullptr)
    {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = nullptr;
    }
}

void updatePlayCursor(bool capture)
{
    static bool held = false;
    ImGuiIO &io = ImGui::GetIO();
    if (capture)
    {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        if (g_window != nullptr)
        {
            SDL_SetWindowMouseGrab(g_window, true);
            SDL_SetWindowRelativeMouseMode(g_window, true);
        }
        SDL_HideCursor();
        held = true;
        return;
    }
    if (held)
    {
        if (g_window != nullptr)
        {
            SDL_SetWindowRelativeMouseMode(g_window, false);
            SDL_SetWindowMouseGrab(g_window, false);
        }
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        held = false;
    }
    SDL_ShowCursor();
}


void armChaseCamera(ChaseCamera &chase, const ViewState &view)
{
    const Vec3 forward = cameraForwardXZ(view);
    chase.yaw = std::atan2(-forward.x, -forward.z);
    chase.pitch = 0.4;
    chase.distance = 4.5;
    chase.ready = true;
    chase.firstPerson = false;
    chase.cWasDown = keyDown(SDLK_C);
    chase.vWasDown = keyDown(SDLK_V);
    chase.shot = -1;
    chase.blending = false;
    chase.blend = 1;
}

PlayInput readPlayInput()
{
    PlayInput input;
    float forward = 0;
    float right = 0;
    if (keyDown(gKeyForward))
        forward += 1;
    if (keyDown(gKeyBack))
        forward -= 1;
    if (keyDown(gKeyRight))
        right += 1;
    if (keyDown(gKeyLeft))
        right -= 1;
    const float scale = std::sqrt(forward * forward + right * right);
    if (scale > 1.0f)
    {
        forward /= scale;
        right /= scale;
    }
    input.moveZ = forward;
    input.moveX = right;
    input.jump = keyDown(gKeyJump) || keyDown(gKeyJumpAlt);
    input.use = keyDown(gKeyUse);
    if (SDL_Gamepad *pad = playGamepad())
    {
        const float ax = gamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 0.2f);
        const float ay = gamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY, 0.2f);
        input.moveX = std::clamp(input.moveX + ax, -1.0f, 1.0f);
        input.moveZ = std::clamp(input.moveZ - ay, -1.0f, 1.0f);
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))
            input.jump = true;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))
            input.use = true;
    }
    return input;
}

bool updatePlayLook(ChaseCamera &chase)
{
    if (!chase.ready || !editorWindowFocused())
        return false;
    bool changed = false;
    const ImGuiIO &io = ImGui::GetIO();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
    {
        chase.yaw -= static_cast<double>(io.MouseDelta.x) * 0.005;
        chase.pitch = std::clamp(chase.pitch - static_cast<double>(io.MouseDelta.y) * 0.005, -1.2, 1.2);
        changed = true;
    }
    if (SDL_Gamepad *pad = playGamepad())
    {
        const float rx = gamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX, 0.2f);
        const float ry = gamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY, 0.2f);
        if (rx != 0.0f || ry != 0.0f)
        {
            const double dt = static_cast<double>(io.DeltaTime);
            chase.yaw -= static_cast<double>(rx) * 2.0 * dt;
            chase.pitch = std::clamp(chase.pitch - static_cast<double>(ry) * 2.0 * dt, -1.2, 1.2);
            changed = true;
        }
    }
    return changed;
}

bool placeChaseCamera(Scene &scene, ViewState &view, const ChaseCamera &chase, const PlayState &state)
{
    if (chase.shot >= 0 && chase.shot < static_cast<int>(scene.shots().size()))
    {
        const SceneCamera &shot = scene.shots()[static_cast<size_t>(chase.shot)];
        view.lookFrom = shot.lookFrom;
        view.lookAt = shot.lookAt;
        view.fov = shot.fov;
        return true;
    }
    if (!chase.ready || state.playerId == kInvalidEntityId)
        return false;
    Object *player = scene.find(state.playerId);
    if (player == nullptr || player->bodyRadius() <= 0)
        return false;
    const double cosPitch = std::cos(chase.pitch);
    const double sinPitch = std::sin(chase.pitch);
    const Vec3 center = player->displayWorldPosition();
    const double radius = player->bodyRadius();
    if (chase.firstPerson)
    {
        const Vec3 forward(-std::sin(chase.yaw), 0.0, -std::cos(chase.yaw));
        view.lookFrom = center + Vec3(0, 0.2, 0) + forward * (radius + 0.08);
        view.lookAt = view.lookFrom + Vec3(
                                          -std::sin(chase.yaw) * cosPitch,
                                          -sinPitch,
                                          -std::cos(chase.yaw) * cosPitch);
        return true;
    }
    const Vec3 lookAt = center + Vec3(0, 0.4, 0);
    view.lookAt = lookAt;
    const Vec3 desired = lookAt + Vec3(
                                   chase.distance * cosPitch * std::sin(chase.yaw),
                                   chase.distance * sinPitch,
                                   chase.distance * cosPitch * std::cos(chase.yaw));
    view.lookFrom = chaseCameraPosition(scene, state.playerId, desired);
    return true;
}

void applyPlayCamera(Scene &scene, ViewState &view, const ChaseCamera &chase, const PlayState &state)
{
    if (!chase.blending)
    {
        placeChaseCamera(scene, view, chase, state);
        return;
    }
    placeChaseCamera(scene, view, chase, state);
    const Vec3 to = view.lookFrom;
    const Vec3 toAt = view.lookAt;
    const double toFov = view.fov;
    const double smooth = chase.blend * chase.blend * (3.0 - 2.0 * chase.blend);
    view.lookFrom = chase.blendFrom * (1.0 - smooth) + to * smooth;
    view.lookAt = chase.blendAt * (1.0 - smooth) + toAt * smooth;
    view.fov = chase.blendFov * (1.0 - smooth) + toFov * smooth;
}

void playSimEvents(Scene &scene, const std::vector<SimEvent> &events, const Vec3 &listener)
{
    if (events.empty())
        return;
    setSoundListener(listener);
    auto playBatch = [&](const std::vector<const SimEvent *> &batch) {
        if (batch.empty())
            return;
        const SimEvent *source = nullptr;
        GameSound sound = GameSound::Beep;
        bool have = false;
        for (const SimEvent *event : batch)
        {
            if (event->kind == SimEventKind::Pickup)
                burstParticles(scene, event->position, Vec3(0.95, 0.85, 0.25));
            else if (event->kind == SimEventKind::Effect)
                burstParticles(scene, event->position, Vec3(0.9, 0.2, 0.15));
            if (event->kind == SimEventKind::Win)
            {
                source = event;
                sound = GameSound::Win;
                have = true;
            }
            else if (event->kind == SimEventKind::Pickup && !(have && sound == GameSound::Win))
            {
                source = event;
                sound = GameSound::Pickup;
                have = true;
            }
            else if (event->kind == SimEventKind::Sound && !(have && (sound == GameSound::Win || sound == GameSound::Pickup)))
            {
                source = event;
                sound = GameSound::Beep;
                have = true;
            }
        }
        if (have && source != nullptr)
            playGameSound(sound, source->position);
    };

    std::vector<const SimEvent *> batch;
    SimTick batchTick = events.front().tick;
    for (const SimEvent &event : events)
    {
        if (event.tick != batchTick)
        {
            playBatch(batch);
            batch.clear();
            batchTick = event.tick;
        }
        batch.push_back(&event);
    }
    playBatch(batch);
}

bool drawPauseMenu(ViewState &view, PlayState &state, bool gameMode, float &timeScale, bool &stepOnce, bool &restart)
{
    if (!view.playing || (!view.paused && state.result.empty()))
        return false;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 center(
        viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const bool over = state.result == "won" || state.result == "lost";
    ImGui::Begin(over ? "Round over" : "Paused", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    bool quitToEditor = false;
    if (over)
        ImGui::TextUnformatted(state.result == "won" ? "You win" : "You lose");
    if (!over && ImGui::Button("Resume", ImVec2(120, 0)))
        view.paused = false;
    if (!over)
        ImGui::SameLine();
    if (!over && ImGui::Button("Step", ImVec2(120, 0)))
        stepOnce = true;
    if (over && ImGui::Button("Restart", ImVec2(120, 0)))
        restart = true;
    if (over)
        ImGui::SameLine();
    if (!over)
        ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(120, 0)))
    {
        if (gameMode)
            requestEditorQuit();
        else
            quitToEditor = true;
    }
    if (!over)
    {
        ImGui::SliderFloat("Speed", &timeScale, 0.25f, 2.0f, "%.2fx");
        ImGui::TextUnformatted("Step runs one 1/30 s tick.");
    }
    ImGui::End();
    return quitToEditor;
}

void drawTitleScreen(ViewState &view)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 center(
        viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Title", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Ray Tracer");
    if (ImGui::Button("Play", ImVec2(160, 0)))
        view.playing = true;
    double volume = masterVolume();
    if (editDouble("Volume", volume, 0.01, 0, 1))
    {
        setMasterVolume(volume);
        saveEditorSettings(view);
    }
    if (ImGui::Button("Quit", ImVec2(160, 0)))
        requestEditorQuit();
    ImGui::End();
}

void drawPlayHud(const Snapshot &snapshot, const ViewState &view, float timeScale, bool firstPerson, const char *cameraLabel)
{
    if (!view.playing)
        return;
    const bool hasPlayer = snapshot.playerId != kInvalidEntityId;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs;
    ImGui::Begin("PlayHud", nullptr, flags);
    if (!hasPlayer)
        ImGui::TextUnformatted("No player");
    else
        ImGui::Text("Score: %d    Health: %d", snapshot.score, snapshot.health);
    if (cameraLabel != nullptr && cameraLabel[0] != '\0')
        ImGui::TextUnformatted(cameraLabel);
    else if (firstPerson)
        ImGui::TextUnformatted("First person");
    if (timeScale < 0.99f || timeScale > 1.01f)
        ImGui::Text("Speed: %.2fx", timeScale);
    if (snapshot.message == "Picked up")
        ImGui::TextUnformatted(snapshot.message.c_str());
    ImGui::End();

    const std::string bannerText = snapshot.message == "Press F" ? std::string("Press ") + nameFromVk(gKeyUse) : snapshot.message;
    if (!bannerText.empty() && bannerText != "Picked up")
    {
        const ImVec2 banner(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + 28.0f);
        ImGui::SetNextWindowPos(banner, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("PlayBanner", nullptr, flags);
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted(bannerText.c_str());
        ImGui::End();
    }

    if (snapshot.result.empty() && g_viewImageShown && (snapshot.lookTag == "use" || snapshot.lookTag == "goal") && snapshot.lookId != kInvalidEntityId)
    {
        const double aspect = view.height > 0 ? static_cast<double>(view.width) / static_cast<double>(view.height) : 1.0;
        const Camera shot = viewCamera(view, aspect);
        const std::string key = nameFromVk(gKeyUse);
        drawWorldPrompt(shot, snapshot.lookPoint, key.c_str(), g_viewImageMin.x, g_viewImageMin.y, g_viewImageMax.x, g_viewImageMax.y);
    }
}


}
