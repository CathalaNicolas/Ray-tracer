#include "EditorInternal.hpp"

#include "DebugDraw.hpp"
#include "Sound.hpp"

namespace ed
{

// ShowCursor is a process-wide count. One call moves it by one, so repeat until
// the returned count is in range. A probe that was already in range is undone.
void matchCursorVisible(bool visible)
{
    if (visible)
    {
        int count = ::ShowCursor(TRUE);
        if (count > 0)
            ::ShowCursor(FALSE);
        else
        {
            while (count < 0)
                count = ::ShowCursor(TRUE);
        }
        return;
    }
    int count = ::ShowCursor(FALSE);
    if (count < -1)
        ::ShowCursor(TRUE);
    else
    {
        while (count >= 0)
            count = ::ShowCursor(FALSE);
    }
}

void updatePlayCursor(bool capture)
{
    static bool held = false;
    if (capture)
    {
        RECT client;
        if (g_hwnd != nullptr && ::GetClientRect(g_hwnd, &client))
        {
            POINT topLeft{client.left, client.top};
            POINT bottomRight{client.right, client.bottom};
            ::ClientToScreen(g_hwnd, &topLeft);
            ::ClientToScreen(g_hwnd, &bottomRight);
            RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
            ::ClipCursor(&screen);
        }
        matchCursorVisible(false);
        held = true;
        return;
    }
    if (held)
    {
        ::ClipCursor(nullptr);
        held = false;
    }
    matchCursorVisible(true);
}


void armChaseCamera(ChaseCamera &chase, const ViewState &view)
{
    const Vec3 forward = cameraForwardXZ(view);
    chase.yaw = std::atan2(-forward.x, -forward.z);
    chase.pitch = 0.4;
    chase.distance = 4.5;
    chase.ready = true;
    chase.firstPerson = false;
    chase.cWasDown = keyDown('C');
    chase.vWasDown = keyDown('V');
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
    return input;
}

bool updatePlayLook(ChaseCamera &chase)
{
    if (!chase.ready || ::GetForegroundWindow() != g_hwnd)
        return false;
    const ImGuiIO &io = ImGui::GetIO();
    if (io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f)
        return false;
    chase.yaw -= static_cast<double>(io.MouseDelta.x) * 0.005;
    chase.pitch = std::clamp(chase.pitch - static_cast<double>(io.MouseDelta.y) * 0.005, -1.2, 1.2);
    return true;
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
    if (!chase.ready || state.playerId < 0)
        return false;
    Hittable *player = scene.find(state.playerId);
    if (player == nullptr || player->bodyRadius() <= 0)
        return false;
    const double cosPitch = std::cos(chase.pitch);
    const double sinPitch = std::sin(chase.pitch);
    const Vec3 center = player->worldPosition();
    const double radius = player->localScale();
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

void playScoreBeep(bool scoreIncreased, const std::string &message, bool becameWon, const Vec3 &listener, const Vec3 &source)
{
    setSoundListener(listener);
    const GameSound sound = (becameWon || message == "You win") ? GameSound::Win
        : (scoreIncreased || message == "Picked up") ? GameSound::Pickup
        : GameSound::Beep;
    playGameSound(sound, source);
}

bool drawPauseMenu(const ViewState &view, PlayState &state, bool gameMode, float &timeScale, bool &stepOnce, bool &restart)
{
    if (!view.playing || !state.paused)
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
        state.paused = false;
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
            ::PostQuitMessage(0);
        else
            quitToEditor = true;
    }
    if (!over)
    {
        ImGui::SliderFloat("Speed", &timeScale, 0.25f, 2.0f, "%.2fx");
        ImGui::TextUnformatted("Step runs one 1/60 s tick.");
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
        ::PostQuitMessage(0);
    ImGui::End();
}

void drawPlayHud(Scene &scene, const ViewState &view, const PlayState &state, float timeScale, bool firstPerson, const char *cameraLabel)
{
    if (!view.playing)
        return;
    const bool hasPlayer = state.playerId >= 0 && scene.find(state.playerId) != nullptr;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs;
    ImGui::Begin("PlayHud", nullptr, flags);
    if (!hasPlayer)
        ImGui::TextUnformatted("No player");
    else
        ImGui::Text("Score: %d    Health: %d", state.score, state.health);
    if (cameraLabel != nullptr && cameraLabel[0] != '\0')
        ImGui::TextUnformatted(cameraLabel);
    else if (firstPerson)
        ImGui::TextUnformatted("First person");
    if (timeScale < 0.99f || timeScale > 1.01f)
        ImGui::Text("Speed: %.2fx", timeScale);
    if (state.message == "Picked up")
        ImGui::TextUnformatted(state.message.c_str());
    ImGui::End();

    const std::string bannerText = state.message == "Press F" ? std::string("Press ") + nameFromVk(gKeyUse) : state.message;
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

    if (state.result.empty() && g_viewImageShown && (state.lookTag == "use" || state.lookTag == "goal") && state.lookId >= 0)
    {
        const double aspect = view.height > 0 ? static_cast<double>(view.width) / static_cast<double>(view.height) : 1.0;
        const Camera shot = viewCamera(view, aspect);
        const std::string key = nameFromVk(gKeyUse);
        drawWorldPrompt(shot, state.lookPoint, key.c_str(), g_viewImageMin.x, g_viewImageMin.y, g_viewImageMax.x, g_viewImageMax.y);
    }
}


}
