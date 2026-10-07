#pragma once

#include "Camera.hpp"
#include "Play.hpp"
#include "SceneFile.hpp"
#include "Vec3.hpp"

#include "imgui.h"

#include <optional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

namespace ed
{

struct ViewState
{
    Vec3 lookFrom{0.15, 1.55, 5.5};
    Vec3 lookAt{0.0, 0.7, 0.15};
    double fov = 42;
    int width = 1920;
    int height = 1080;
    int samples = 1;
    int depth = 3;
    double aperture = 0;
    double focusDistance = 5;
    int selectedObject = -1;
    std::vector<int> alsoSelected;
    int selectedLight = -1;
    int selectedCamera = -1;
    Vec3 editorFrom{0.15, 1.55, 5.5};
    Vec3 editorAt{0.0, 0.7, 0.15};
    double editorFov = 42;
    bool playing = false;
    bool showColliders = false;
    bool showBounceRays = false;
    int gizmoMode = 0;
};

struct WglWindow
{
    HDC dc = nullptr;
};

struct ChaseCamera
{
    double yaw = 0;
    double pitch = 0.4;
    double distance = 4.5;
    bool ready = false;
    bool firstPerson = false;
    bool cWasDown = false;
    bool vWasDown = false;
    int shot = -1;
    bool blending = false;
    double blend = 1;
    Vec3 blendFrom{0, 0, 0};
    Vec3 blendAt{0, 0, 0};
    double blendFov = 42;
};

struct EditorViewSnapshot
{
    Vec3 lookFrom;
    Vec3 lookAt;
    double fov = 42;
    double aperture = 0;
    double focusDistance = 5;
    int width = 1920;
    int height = 1080;
    int samples = 1;
    int depth = 3;
    int selectedObject = -1;
    std::vector<int> alsoSelected;
    int selectedLight = -1;
    int selectedCamera = -1;
    Vec3 editorFrom{0.15, 1.55, 5.5};
    Vec3 editorAt{0.0, 0.7, 0.15};
    double editorFov = 42;
    bool showColliders = false;
};

struct EditorSnapshot
{
    Scene scene;
    EditorViewSnapshot view;
};

struct EditorHistory
{
    std::vector<EditorSnapshot> undo;
    std::vector<EditorSnapshot> redo;
    bool pushedThisFrame = false;
};

constexpr int kEditorUndoDepth = 32;

extern HGLRC g_glContext;
extern HWND g_hwnd;
extern int g_windowWidth;
extern int g_windowHeight;
extern ImVec2 g_viewImageMin;
extern ImVec2 g_viewImageMax;
extern bool g_viewImageShown;
extern std::string gAssetPath;
extern int gKeyForward;
extern int gKeyBack;
extern int gKeyLeft;
extern int gKeyRight;
extern int gKeyJump;
extern int gKeyJumpAlt;
extern int gKeyUse;
extern int gCaptureBind;
extern int gSavedWidth;
extern int gSavedHeight;
extern int gSavedSamples;
extern int gSavedBounces;
extern bool gStopPrompt;
extern std::optional<Scene> gKeptScene;
extern EditorHistory editorHistory;

std::string nameFromVk(int vk);
void loadEditorSettings();
void saveEditorSettings(const ViewState &view);
bool editDouble(const char *label, double &value, double speed, double minValue, double maxValue);
bool editVec3(const char *label, Vec3 &value, float speed);
bool editColor(const char *label, Vec3 &value);
bool editMaterial(Hittable &object);
bool editName(int id, std::string &name);
std::string filenameOf(const std::string &stored);
Camera viewCamera(const ViewState &view, double aspect);
bool keyDown(int key);
Vec3 cameraForwardXZ(const ViewState &view);
bool assetIsMesh(const std::string &path);
bool assetIsImage(const std::string &path);
Vec3 snapVec(const Vec3 &value);
bool pickScenePath(bool save, std::filesystem::path &path);

void updatePlayCursor(bool capture);
void armChaseCamera(ChaseCamera &chase, const ViewState &view);
PlayInput readPlayInput();
bool updatePlayLook(ChaseCamera &chase);
bool placeChaseCamera(Scene &scene, ViewState &view, const ChaseCamera &chase, const PlayState &state);
void applyPlayCamera(Scene &scene, ViewState &view, const ChaseCamera &chase, const PlayState &state);
void playScoreBeep(bool scoreIncreased, const std::string &message, bool becameWon, const Vec3 &listener, const Vec3 &source);
bool drawPauseMenu(const ViewState &view, PlayState &state, bool gameMode, float &timeScale, bool &stepOnce, bool &restart);
void drawTitleScreen(ViewState &view);
void drawPlayHud(Scene &scene, const ViewState &view, const PlayState &state, float timeScale, bool firstPerson, const char *cameraLabel);

bool objectChosen(const ViewState &view, int id);
void chooseObject(ViewState &view, int id, bool extend);
bool materialScrolling(const Scene &scene);
bool refreshAssets(Scene &scene);
void duplicateSelection(Scene &scene, ViewState &view);
void deleteSelection(Scene &scene, ViewState &view);
int addSphere(Scene &scene);
int addPlane(Scene &scene);
bool drawObjectSettings(Hittable &object, const Scene &scene);
Vec3 gizmoAxisDir(int axis);
bool handleViewMouse(Scene &scene, ViewState &view, float drawWidth, float drawHeight, double aspect);

EditorViewSnapshot captureEditorView(const ViewState &view);
EditorSnapshot captureEditorSnapshot(const Scene &scene, const ViewState &view);
void pushEditorSnapshot(EditorSnapshot snap);
void pushEditorHistory(const Scene &scene, const ViewState &view);
bool undoEditor(Scene &scene, ViewState &view);
bool redoEditor(Scene &scene, ViewState &view);

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
    bool gameMode);

}
