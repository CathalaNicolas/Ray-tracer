#include "EditorInternal.hpp"

#include "imgui_internal.h"

#include <unordered_map>

#include "DebugDraw.hpp"
#include "EngineSettings.hpp"
#include "GpuLimits.hpp"
#include "SceneDebug.hpp"
#include "Sound.hpp"

#include <fstream>

namespace ed
{

bool drawInterface(
    Scene &scene,
    ViewState &view,
    ImTextureID texture,
    bool hasImage,
    int imageWidth,
    int imageHeight,
    int renderMs,
    int refineCount,
    int refineTarget,
    const char *deviceLabel,
    const std::string &gpuFailure,
    std::string &notice,
    bool &saveRequested,
    bool &savePlayRequested,
    bool &loadPlayRequested,
    bool gameMode)
{
    bool dirty = false;
    editorHistory.pushedThisFrame = false;
    EditorSnapshot frameBefore;
    if (!view.playing)
        frameBefore = captureEditorSnapshot(scene, view);

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Ray Tracer", nullptr, rootFlags);

    if (!gameMode)
    {
    auto showCamera = [&](int index) {
        if (view.selectedCamera < 0)
        {
            view.editorFrom = view.lookFrom;
            view.editorAt = view.lookAt;
            view.editorFov = view.fov;
        }
        else if (view.selectedCamera < static_cast<int>(scene.shots().size()))
        {
            SceneCamera &leaving = scene.shots()[static_cast<size_t>(view.selectedCamera)];
            leaving.lookFrom = view.lookFrom;
            leaving.lookAt = view.lookAt;
            leaving.fov = view.fov;
        }
        view.selectedCamera = index;
        view.selectedObject = kInvalidEntityId;
        view.alsoSelected.clear();
        view.selectedLight = -1;
        if (index < 0)
        {
            view.lookFrom = view.editorFrom;
            view.lookAt = view.editorAt;
            view.fov = view.editorFov;
        }
        else if (index < static_cast<int>(scene.shots().size()))
        {
            const SceneCamera &chosen = scene.shots()[static_cast<size_t>(index)];
            view.lookFrom = chosen.lookFrom;
            view.lookAt = chosen.lookAt;
            view.fov = chosen.fov;
        }
        dirty = true;
    };
    auto addCameraFromView = [&]() {
        if (view.playing)
            return;
        pushEditorHistory(scene, view);
        SceneCamera shot;
        shot.name = "Camera " + std::to_string(scene.shots().size() + 1);
        shot.lookFrom = view.lookFrom;
        shot.lookAt = view.lookAt;
        shot.fov = view.fov;
        scene.shots().push_back(shot);
        notice = "Added " + shot.name;
        showCamera(static_cast<int>(scene.shots().size()) - 1);
    };
    const bool textIdle = !ImGui::GetIO().WantTextInput;
    if (!view.playing && textIdle && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_C))
        addCameraFromView();

    if (ImGui::BeginTable("toolbar", 3, ImGuiTableFlags_SizingStretchProp))
    {
    ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableNextColumn();
    if (view.playing)
        ImGui::BeginDisabled();
    if (ImGui::Button("Add"))
        ImGui::OpenPopup("add_menu");
    if (ImGui::BeginPopup("add_menu"))
    {
        if (ImGui::MenuItem("Sphere"))
        {
            pushEditorHistory(scene, view);
            view.selectedObject = addSphere(scene);
            view.selectedLight = -1;
            dirty = true;
        }
        if (ImGui::MenuItem("Plane"))
        {
            pushEditorHistory(scene, view);
            view.selectedObject = addPlane(scene);
            view.selectedLight = -1;
            dirty = true;
        }
        if (ImGui::MenuItem("Mesh"))
        {
            if (!assetIsMesh(gAssetPath))
                notice = "Select a mesh in Assets";
            else
            {
                Object *mesh = scene.addMesh();
                std::string error;
                if (!mesh->load(gAssetPath, error))
                {
                    scene.remove(mesh->id());
                    notice = error;
                }
                else
                {
                    mesh->name() = std::filesystem::path(gAssetPath).stem().string();
                    if (mesh->name().empty())
                        mesh->name() = "Mesh";
                    pushEditorHistory(scene, view);
                    view.selectedObject = mesh->id();
                    view.selectedLight = -1;
                    dirty = true;
                }
            }
        }
        if (ImGui::MenuItem("Light"))
        {
            pushEditorHistory(scene, view);
            PointLight light(Vec3(0, 4, 2), Vec3(1, 1, 1), 1, 0.02);
            scene.addLight(light);
            view.selectedLight = static_cast<int>(scene.lights().size()) - 1;
            view.selectedObject = kInvalidEntityId;
            scene.lights().back().name = "Light " + std::to_string(view.selectedLight + 1);
            dirty = true;
        }
        if (ImGui::MenuItem("Camera", "Ctrl+Shift+C"))
            addCameraFromView();
        EntityId prefabSource = kInvalidEntityId;
        if (view.selectedObject != kInvalidEntityId)
        {
            if (const Object *selected = scene.find(view.selectedObject))
            {
                if (!selected->prefab().empty())
                    prefabSource = selected->id();
                else if (!selected->instanceOf().empty())
                {
                    for (const auto &candidate : scene.objects())
                    {
                        if (candidate->prefab() == selected->instanceOf())
                        {
                            prefabSource = candidate->id();
                            break;
                        }
                    }
                }
            }
        }
        if (prefabSource == kInvalidEntityId)
        {
            for (const auto &candidate : scene.objects())
            {
                if (!candidate->prefab().empty())
                {
                    prefabSource = candidate->id();
                    break;
                }
            }
        }
        if (ImGui::MenuItem("Prefab", nullptr, false, prefabSource != kInvalidEntityId))
        {
            pushEditorHistory(scene, view);
            view.selectedObject = placePrefabInstance(scene, prefabSource);
            view.selectedLight = -1;
            view.alsoSelected.clear();
            dirty = true;
        }
        ImGui::EndPopup();
    }
    if (view.playing)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Edit"))
        ImGui::OpenPopup("edit_menu");
    if (ImGui::BeginPopup("edit_menu"))
    {
        const bool canDuplicate = !view.playing && view.selectedObject != kInvalidEntityId;
        if (ImGui::MenuItem("Duplicate", nullptr, false, canDuplicate))
        {
            pushEditorHistory(scene, view);
            duplicateSelection(scene, view);
            dirty = true;
        }
        const bool canDelete = !view.playing && (view.selectedObject != kInvalidEntityId || view.selectedLight >= 0 || view.selectedCamera >= 0);
        if (ImGui::MenuItem("Delete", "Del", false, canDelete))
        {
            pushEditorHistory(scene, view);
            deleteSelection(scene, view);
            dirty = true;
        }
        if (ImGui::MenuItem("Save PNG", nullptr, false, hasImage))
            saveRequested = true;
        const bool canPrefab = !view.playing && view.selectedObject != kInvalidEntityId;
        if (ImGui::MenuItem("Make prefab", nullptr, false, canPrefab))
        {
            if (Object *selected = scene.find(view.selectedObject))
            {
                selected->instanceOf().clear();
                selected->prefab() = selected->name().empty() ? "Prefab" : selected->name();
                dirty = true;
            }
        }
        ImGui::EndPopup();
    }
    if (!view.playing && textIdle && (view.selectedObject != kInvalidEntityId || view.selectedLight >= 0 || view.selectedCamera >= 0) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive())
    {
        pushEditorHistory(scene, view);
        deleteSelection(scene, view);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("File"))
        ImGui::OpenPopup("file_menu");
    if (ImGui::BeginPopup("file_menu"))
    {
        if (ImGui::MenuItem("Save scene", nullptr, false, !view.playing))
        {
            std::filesystem::path path;
            if (pickScenePath(true, path))
            {
                const Vec3 from = view.selectedCamera < 0 ? view.lookFrom : view.editorFrom;
                const Vec3 at = view.selectedCamera < 0 ? view.lookAt : view.editorAt;
                const double fov = view.selectedCamera < 0 ? view.fov : view.editorFov;
                CameraSetup camera{from, at, Vec3(0, 1, 0), fov, view.aperture, view.focusDistance};
                std::string error;
                if (saveScene(path, scene, camera, error))
                    notice = "Saved " + path.filename().string();
                else
                    notice = error;
            }
        }
        if (ImGui::MenuItem("Save play session", nullptr, false, view.playing))
            savePlayRequested = true;
        if (ImGui::MenuItem("Load play session", nullptr, false, view.playing))
            loadPlayRequested = true;
        if (ImGui::MenuItem("Load scene", nullptr, false, !view.playing))
        {
            std::filesystem::path path;
            if (pickScenePath(false, path))
            {
                CameraSetup camera{view.lookFrom, view.lookAt, Vec3(0, 1, 0), view.fov};
                std::string error;
                EditorSnapshot beforeLoad = captureEditorSnapshot(scene, view);
                if (loadScene(path, scene, camera, error))
                {
                    pushEditorSnapshot(std::move(beforeLoad));
                    view.lookFrom = camera.lookFrom;
                    view.lookAt = camera.lookAt;
                    view.fov = camera.fovY;
                    view.editorFrom = camera.lookFrom;
                    view.editorAt = camera.lookAt;
                    view.editorFov = camera.fovY;
                    view.selectedCamera = -1;
                    view.aperture = camera.aperture;
                    view.focusDistance = camera.focusDistance > 1e-4 ? camera.focusDistance : length(camera.lookAt - camera.lookFrom);
                    view.selectedObject = kInvalidEntityId;
                    view.alsoSelected.clear();
                    view.selectedLight = -1;
                    notice = "Loaded " + path.filename().string();
                    dirty = true;
                }
                else
                {
                    notice = error;
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings"))
        ImGui::OpenPopup("settings_menu");
    if (ImGui::BeginPopup("settings_menu"))
    {
        ImGui::TextUnformatted("Render");
        dirty |= ImGui::SliderInt("Width", &view.width, 160, 3840);
        dirty |= ImGui::SliderInt("Height", &view.height, 120, 2160);
        if (view.playing)
            ImGui::BeginDisabled();
        dirty |= ImGui::SliderInt("Samples", &view.samples, 1, 4);
        if (view.playing)
            ImGui::EndDisabled();
        dirty |= ImGui::SliderInt("Bounces", &view.depth, 0, 6);
        EngineSettings &settings = engineSettings();
        if (ImGui::Checkbox("Mirror bounces", &settings.mirrorBounces))
        {
            dirty = true;
            saveEditorSettings(view);
            notice = "Saved settings";
        }
        dirty |= editDouble("Mirror reflect min", settings.mirrorReflectMin, 0.01, 0.05, 1);
        dirty |= ImGui::SliderInt("Mesh trace limit", &settings.meshTraceLimit, 8, 512);
        dirty |= ImGui::SliderInt("Job stack", &settings.jobStackLimit, 1, kGlslJobStackMax);
        dirty |= ImGui::SliderInt("Trace limit", &settings.traceLimit, 1, kGlslTraceLimitMax);
        dirty |= ImGui::SliderInt("Mesh stack", &settings.meshStackLimit, 2, kGlslMeshStackMax);
        dirty |= editDouble("Bloom threshold", settings.bloomThreshold, 0.01, 0.5, 2);
        dirty |= editDouble("Bloom strength", settings.bloomStrength, 0.01, 0, 2);
        dirty |= editDouble("View distance", settings.viewDistance, 10, 20, 4000);
        dirty |= ImGui::SliderInt("Shadow map", &settings.shadowMapSize, 256, 2048);
        dirty |= editDouble("Particle density", settings.particleDensity, 0.05, 0, 2);
        ImGui::TextDisabled("Samples is the still-image grid. Play draws one sample.");
        ImGui::Separator();
        ImGui::TextUnformatted("Play");
        dirty |= editDouble("Move speed", settings.moveSpeed, 0.05, 0.5, 20);
        dirty |= editDouble("Gravity", settings.gravity, 0.1, -40, 0);
        dirty |= editDouble("Jump speed", settings.jumpSpeed, 0.05, 0.5, 20);
        dirty |= editDouble("Ground probe", settings.groundProbe, 0.005, 0.01, 1);
        dirty |= editDouble("Step height", settings.stepHeight, 0.01, 0.05, 2);
        dirty |= editDouble("Fall Y", settings.fallY, 0.1, -40, 0);
        dirty |= editDouble("Action duration", settings.actionDuration, 0.01, 0.05, 4);
        dirty |= editDouble("Play ray", settings.playRayDistance, 0.05, 0.5, 20);
        dirty |= ImGui::SliderInt("Max tweens", &settings.maxTweens, 1, 32);
        dirty |= ImGui::SliderInt("Max play steps", &settings.maxPlayStepsPerFrame, 1, 16);
        dirty |= editDouble("Max frame dt", settings.maxPlayFrameDt, 0.01, 0.05, 1);
        dirty |= ImGui::SliderInt("Min substeps", &settings.minSubsteps, 1, 64);
        dirty |= ImGui::SliderInt("Max substeps", &settings.maxSubsteps, 1, 128);
        dirty |= ImGui::SliderInt("Resolve passes", &settings.resolvePasses, 1, 16);
        dirty |= ImGui::SliderInt("Max particles", &settings.maxParticles, 1, 256);
        dirty |= editDouble("Sound far", settings.soundFar, 0.25, 1, 64);
        ImGui::Separator();
        ImGui::TextUnformatted("Debug overlay");
        dirty |= editDouble("Frustum near", settings.frustumNear, 0.05, 0.05, 4);
        dirty |= editDouble("Frustum far", settings.frustumFar, 0.25, 1, 64);
        ImGui::Separator();
        ImGui::TextUnformatted("Keys");
        auto bindRow = [&](const char *label, int id, int vk) {
            const std::string caption = std::string(label) + ": " + (gCaptureBind == id ? std::string("Press a key") : nameFromVk(vk));
            if (ImGui::Button(caption.c_str(), ImVec2(180, 0)))
                gCaptureBind = id;
        };
        bindRow("Forward", 1, gKeyForward);
        bindRow("Back", 2, gKeyBack);
        bindRow("Left", 3, gKeyLeft);
        bindRow("Right", 4, gKeyRight);
        bindRow("Jump", 5, gKeyJump);
        bindRow("Jump 2", 6, gKeyJumpAlt);
        bindRow("Use", 7, gKeyUse);
        if (gCaptureBind != 0)
        {
            int found = 0;
            int keyCount = 0;
            const bool *keys = SDL_GetKeyboardState(&keyCount);
            if (keys != nullptr)
            {
                for (int sc = SDL_SCANCODE_A; sc < keyCount; ++sc)
                {
                    if (!keys[sc] || sc == SDL_SCANCODE_ESCAPE)
                        continue;
                    const SDL_Keycode key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
                    if (key == SDLK_UNKNOWN)
                        continue;
                    found = static_cast<int>(key);
                    break;
                }
            }
            if (found != 0)
            {
                int *slot = &gKeyForward;
                if (gCaptureBind == 2)
                    slot = &gKeyBack;
                else if (gCaptureBind == 3)
                    slot = &gKeyLeft;
                else if (gCaptureBind == 4)
                    slot = &gKeyRight;
                else if (gCaptureBind == 5)
                    slot = &gKeyJump;
                else if (gCaptureBind == 6)
                    slot = &gKeyJumpAlt;
                else if (gCaptureBind == 7)
                    slot = &gKeyUse;
                *slot = found;
                gCaptureBind = 0;
                saveEditorSettings(view);
                notice = "Saved settings";
            }
        }
        if (ImGui::Button("Save settings"))
        {
            saveEditorSettings(view);
            notice = "Saved settings";
        }
        ImGui::EndPopup();
    }
    ImGui::TableNextColumn();
    const float playSide = ImGui::GetFrameHeight();
    if (ImGui::Button("##playstop", ImVec2(playSide, playSide)))
    {
        if (view.playing && !gameMode)
            gStopPrompt = true;
        else
            view.playing = !view.playing;
    }
    {
        const ImVec2 boxMin = ImGui::GetItemRectMin();
        const ImVec2 boxMax = ImGui::GetItemRectMax();
        const ImVec2 center((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);
        const ImU32 icon = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList *icons = ImGui::GetWindowDrawList();
        if (view.playing)
            icons->AddRectFilled(ImVec2(center.x - 5.0f, center.y - 5.0f), ImVec2(center.x + 5.0f, center.y + 5.0f), icon);
        else
            icons->AddTriangleFilled(ImVec2(center.x - 4.0f, center.y - 6.0f), ImVec2(center.x - 4.0f, center.y + 6.0f), ImVec2(center.x + 7.0f, center.y), icon);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", view.playing ? "Stop" : "Play");
    ImGui::TableNextColumn();
    if (view.playing)
        ImGui::BeginDisabled();
    ImGui::RadioButton("Move", &view.gizmoMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &view.gizmoMode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Scale", &view.gizmoMode, 2);
    if (view.playing)
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &view.showColliders);
    ImGui::SameLine();
    ImGui::Checkbox("Bounce rays", &view.showBounceRays);
    ImGui::SameLine();
    if (ImGui::Button("Debug dump"))
    {
        const double aspect = imageHeight > 0 ? static_cast<double>(imageWidth) / static_cast<double>(imageHeight) : 1.0;
        const Camera shot = viewCamera(view, aspect);
        const std::string text = sceneDebugText(scene, shot, view.lookFrom, view.lookAt, view.fov, view.aperture, view.focusDistance, view.width, view.height, view.samples, view.depth, view.selectedObject, view.selectedLight);
        std::ofstream file("raytracer-debug.txt", std::ios::binary);
        if (file)
        {
            file << text;
            notice = "Wrote raytracer-debug.txt";
        }
        else
            notice = "Could not write raytracer-debug.txt";
    }
    ImGui::SameLine();
    if (view.playing)
        ImGui::Text("Playing  %d ms", renderMs);
    else if (refineTarget > 1 && refineCount > 0 && refineCount < refineTarget)
        ImGui::Text("Refining %d/%d  %d ms", refineCount, refineTarget, renderMs);
    else
        ImGui::Text("Ready  %d ms", renderMs);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", deviceLabel);
    {
        const GpuSceneLimits limits = gpuSceneLimits(scene);
        const std::string limitWarning = limits.warning();
        if (!limitWarning.empty() && gpuFailure.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", limitWarning.c_str());
        const std::string stackWarning = limits.stackWarning(view.depth);
        if (!stackWarning.empty() && gpuFailure.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", stackWarning.c_str());
    }
    if (!notice.empty())
        ImGui::TextDisabled("%s", notice.c_str());
    ImGui::EndTable();
    }

    ImGui::BeginChild("scene", ImVec2(280, 0), ImGuiChildFlags_Borders);
    static char outlinerFilter[128] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##outliner-filter", "Search", outlinerFilter, sizeof(outlinerFilter));
    struct OutlinerRow
    {
        std::string name;
        std::string kind;
        std::string text;
        std::string folded;
        bool foldReady = false;
    };
    static std::unordered_map<EntityId, OutlinerRow> objectRows;
    static std::unordered_map<int, OutlinerRow> lightRows;
    static std::unordered_map<int, OutlinerRow> cameraRows;
    const bool filtering = outlinerFilter[0] != '\0';
    std::string needle;
    auto lower = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    };
    if (filtering)
    {
        needle = outlinerFilter;
        for (char &c : needle)
            c = lower(static_cast<unsigned char>(c));
    }
    const auto matchesOutliner = [&](OutlinerRow &row) {
        if (!filtering)
            return true;
        if (!row.foldReady)
        {
            row.folded = row.text;
            for (char &c : row.folded)
                c = lower(static_cast<unsigned char>(c));
            row.foldReady = true;
        }
        return row.folded.find(needle) != std::string::npos;
    };
    const auto reuseRow = [](std::unordered_map<int, OutlinerRow> &rows, int key, const std::string &name, const char *kind) -> OutlinerRow & {
        OutlinerRow &row = rows[key];
        if (row.name != name || row.kind != kind)
        {
            row.name = name;
            row.kind = kind;
            row.text = name.empty() ? std::string(kind) + " " + std::to_string(key + 1) : name;
            row.folded.clear();
            row.foldReady = false;
        }
        return row;
    };
    ImGui::SeparatorText("Objects");
    for (const auto &object : scene.objects())
    {
        const char *kind = object->kind();
        OutlinerRow &row = objectRows[object->id()];
        if (row.name != object->name() || row.kind != kind)
        {
            row.name = object->name();
            row.kind = kind;
            row.text = object->name().empty() ? row.kind : object->name();
            row.folded.clear();
            row.foldReady = false;
        }
        if (!matchesOutliner(row))
            continue;
        ImGui::PushID(static_cast<int>(object->id() & 0xffffffffu));
        ImGui::PushID(static_cast<int>(object->id() >> 32));
        if (ImGui::Selectable(row.text.c_str(), objectChosen(view, object->id())))
        {
            const bool extend = ImGui::GetIO().KeyShift;
            if (extend || !objectChosen(view, object->id()))
            {
                chooseObject(view, object->id(), extend);
                dirty = true;
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::SeparatorText("Lights");
    for (size_t index = 0; index < scene.lights().size(); ++index)
    {
        const PointLight &light = scene.lights()[index];
        OutlinerRow &row = reuseRow(lightRows, static_cast<int>(index), light.name, "Light");
        if (!matchesOutliner(row))
            continue;
        ImGui::PushID(static_cast<int>(index) + 10000);
        if (ImGui::Selectable(row.text.c_str(), view.selectedLight == static_cast<int>(index)))
        {
            const bool clearHighlight = view.selectedObject != kInvalidEntityId;
            view.selectedLight = static_cast<int>(index);
            view.selectedObject = kInvalidEntityId;
            view.alsoSelected.clear();
            dirty = clearHighlight;
        }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Cameras");
    if (view.selectedCamera >= static_cast<int>(scene.shots().size()))
        view.selectedCamera = -1;
    if (ImGui::Selectable("Editor Camera", view.selectedCamera < 0) && view.selectedCamera >= 0)
        showCamera(-1);
    for (size_t index = 0; index < scene.shots().size(); ++index)
    {
        const SceneCamera &shot = scene.shots()[index];
        OutlinerRow &row = reuseRow(cameraRows, static_cast<int>(index), shot.name, "Camera");
        if (!matchesOutliner(row))
            continue;
        ImGui::PushID(static_cast<int>(index) + 30000);
        if (ImGui::Selectable(row.text.c_str(), view.selectedCamera == static_cast<int>(index)) && view.selectedCamera != static_cast<int>(index))
            showCamera(static_cast<int>(index));
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    }
    ImGui::BeginChild("view", gameMode ? ImVec2(0, 0) : ImVec2(-360, 0), ImGuiChildFlags_None);
    g_viewImageShown = false;
    if (hasImage && texture != 0)
    {
        ImVec2 available = ImGui::GetContentRegionAvail();
        float aspect = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
        float drawWidth = available.x;
        float drawHeight = drawWidth / aspect;
        if (drawHeight > available.y)
        {
            drawHeight = available.y;
            drawWidth = drawHeight * aspect;
        }
        float offsetX = (available.x - drawWidth) * 0.5f;
        float offsetY = (available.y - drawHeight) * 0.5f;
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));
        ImGui::Image(texture, ImVec2(drawWidth, drawHeight));
        g_viewImageMin = ImGui::GetItemRectMin();
        g_viewImageMax = ImGui::GetItemRectMax();
        g_viewImageShown = true;
        if (imageWidth > 0 && imageHeight > 0 && (view.showColliders || view.showBounceRays))
        {
            const Camera shot = viewCamera(view, static_cast<double>(imageWidth) / static_cast<double>(imageHeight));
            if (view.showColliders)
                drawColliders(scene, shot, kInvalidEntityId, g_viewImageMin.x, g_viewImageMin.y, g_viewImageMax.x, g_viewImageMax.y);
            if (view.showBounceRays)
                drawBounceRays(scene, shot, g_viewImageMin.x, g_viewImageMin.y, g_viewImageMax.x, g_viewImageMax.y);
        }
        if (!view.playing && view.selectedObject != kInvalidEntityId && imageWidth > 0 && imageHeight > 0)
        {
            const Camera shot = viewCamera(view, static_cast<double>(imageWidth) / static_cast<double>(imageHeight));
            drawGizmo(scene, view.selectedObject, view.gizmoMode, shot, g_viewImageMin.x, g_viewImageMin.y, g_viewImageMax.x, g_viewImageMax.y);
        }
        dirty |= handleViewMouse(scene, view, drawWidth, drawHeight, aspect);
    }
    else if (!gpuFailure.empty())
    {
        ImGui::TextWrapped("%s", gpuFailure.c_str());
    }
    else
    {
        ImGui::TextUnformatted("Rendering...");
    }
    ImGui::EndChild();

    if (!gameMode)
    {
    ImGui::SameLine();
    ImGui::BeginChild("inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Select a file, then Add mesh or an image button.");
        if (std::filesystem::exists("assets"))
        {
            for (const auto &entry : std::filesystem::directory_iterator("assets"))
            {
                if (!entry.is_regular_file())
                    continue;
                const std::string path = entry.path().generic_string();
                if (!assetIsMesh(path) && !assetIsImage(path))
                    continue;
                const std::string label = entry.path().filename().string();
                if (ImGui::Selectable(label.c_str(), gAssetPath == path))
                    gAssetPath = path;
            }
        }
        if (!gAssetPath.empty())
            ImGui::TextWrapped("%s", gAssetPath.c_str());
    }
    if (ImGui::CollapsingHeader("World"))
    {
        Vec3 ambient = scene.ambient();
        if (editColor("Ambient", ambient))
        {
            scene.setAmbient(ambient);
            dirty = true;
        }
        Vec3 horizon = scene.horizon();
        Vec3 zenith = scene.zenith();
        bool backgroundChanged = editColor("Horizon", horizon);
        backgroundChanged |= editColor("Zenith", zenith);
        if (backgroundChanged)
        {
            scene.setBackground(horizon, zenith);
            dirty = true;
        }
        if (ImGui::Button("Environment map"))
        {
            if (assetIsImage(gAssetPath))
            {
                scene.setEnvironment(gAssetPath);
                dirty = true;
            }
        }
        if (!scene.environment().empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear map"))
            {
                scene.setEnvironment("");
                dirty = true;
            }
            ImGui::TextWrapped("%s", filenameOf(scene.environment()).c_str());
        }
        Vec3 fog = scene.fogColor();
        double fogDensity = scene.fogDensity();
        bool fogChanged = editColor("Fog", fog);
        fogChanged = editDouble("Fog density", fogDensity, 0.01, 0.0, 2.0) || fogChanged;
        if (fogChanged)
        {
            scene.setFog(fog, fogDensity);
            dirty = true;
        }
        double volume = masterVolume();
        if (editDouble("Volume", volume, 0.01, 0, 1))
            setMasterVolume(volume);
        editDouble("Snap", engineSettings().snap, 0.01, 0.05, 4);
        ImGui::TextDisabled("Hold Ctrl to snap moves and position sliders.");
        double exposure = scene.exposure();
        if (editDouble("Exposure", exposure, 0.02, 0.05, 8))
        {
            scene.setExposure(exposure);
            dirty = true;
        }
    }

    ImGui::SeparatorText("Selection");
    if (view.selectedObject != kInvalidEntityId)
    {
        if (Object *object = scene.find(view.selectedObject))
            dirty |= drawObjectSettings(*object, scene);
        else
            view.selectedObject = kInvalidEntityId;
    }
    else if (view.selectedLight >= 0 && static_cast<size_t>(view.selectedLight) < scene.lights().size())
    {
        PointLight &light = scene.lights()[static_cast<size_t>(view.selectedLight)];
        editName(view.selectedLight + 100000, light.name);
        bool directional = light.directional;
        if (ImGui::Checkbox("Directional", &directional))
        {
            light.directional = directional;
            dirty = true;
        }
        dirty |= editVec3(directional ? "Direction" : "Position", light.position, 0.05f);
        dirty |= editColor("Color", light.color);
        dirty |= editDouble("Intensity", light.intensity, 0.02, 0, 8);
        if (directional)
            dirty |= editDouble("Softness", light.radius, 0.1, 0, 25);
        else
        {
            dirty |= editDouble("Radius", light.radius, 0.01, 0, 5);
            dirty |= editDouble("Falloff", light.falloff, 0.001, 0, 0.2);
        }
        dirty |= editVec3("Spot direction", light.spotDirection, 0.05f);
        dirty |= editDouble("Spot outer", light.spotOuter, 0.2, 0, 89);
        dirty |= editDouble("Spot inner", light.spotInner, 0.2, 0, 89);
    }
    else if (view.selectedCamera >= 0 && view.selectedCamera < static_cast<int>(scene.shots().size()))
    {
        SceneCamera &shot = scene.shots()[static_cast<size_t>(view.selectedCamera)];
        editName(50000 + view.selectedCamera, shot.name);
        dirty |= editVec3("From", view.lookFrom, 0.05f);
        dirty |= editVec3("Target", view.lookAt, 0.05f);
        dirty |= editDouble("Field of view", view.fov, 0.2, 10, 120);
    }
    else
    {
        ImGui::TextUnformatted("Editor Camera");
        dirty |= editVec3("From", view.lookFrom, 0.05f);
        dirty |= editVec3("Target", view.lookAt, 0.05f);
        dirty |= editDouble("Field of view", view.fov, 0.2, 15, 120);
        dirty |= editDouble("Aperture", view.aperture, 0.002, 0, 1.5);
        dirty |= editDouble("Focus", view.focusDistance, 0.02, 0.05, 80);
    }
    ImGui::EndChild();
    }
    ImGui::End();
    const ImGuiContext &imgui = *GImGui;
    const bool itemActivated = imgui.ActiveId != 0 && imgui.ActiveId != imgui.ActiveIdPreviousFrame;
    if (!view.playing && !editorHistory.pushedThisFrame && itemActivated)
        pushEditorSnapshot(std::move(frameBefore));
    if (!view.playing && !ImGui::GetIO().WantTextInput)
    {
        const bool redo = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)
            || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
        const bool undo = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z);
        if (redo)
            dirty |= redoEditor(scene, view);
        else if (undo)
            dirty |= undoEditor(scene, view);
    }
    if (!view.playing)
    {
        if (view.selectedCamera < 0)
        {
            view.editorFrom = view.lookFrom;
            view.editorAt = view.lookAt;
            view.editorFov = view.fov;
        }
        else if (view.selectedCamera < static_cast<int>(scene.shots().size()))
        {
            SceneCamera &shot = scene.shots()[static_cast<size_t>(view.selectedCamera)];
            shot.lookFrom = view.lookFrom;
            shot.lookAt = view.lookAt;
            shot.fov = view.fov;
        }
    }
    return dirty;
}


}
