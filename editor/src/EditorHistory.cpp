#include "EditorInternal.hpp"

namespace ed
{

EditorViewSnapshot captureEditorView(const ViewState &view)
{
    EditorViewSnapshot snap;
    snap.lookFrom = view.lookFrom;
    snap.lookAt = view.lookAt;
    snap.fov = view.fov;
    snap.aperture = view.aperture;
    snap.focusDistance = view.focusDistance;
    snap.width = view.width;
    snap.height = view.height;
    snap.samples = view.samples;
    snap.depth = view.depth;
    snap.selectedObject = view.selectedObject;
    snap.alsoSelected = view.alsoSelected;
    snap.selectedLight = view.selectedLight;
    snap.selectedCamera = view.selectedCamera;
    snap.editorFrom = view.editorFrom;
    snap.editorAt = view.editorAt;
    snap.editorFov = view.editorFov;
    snap.showColliders = view.showColliders;
    return snap;
}

EditorSnapshot captureEditorSnapshot(const Scene &scene, const ViewState &view)
{
    EditorSnapshot snap;
    snap.scene = scene.clone();
    snap.view = captureEditorView(view);
    return snap;
}

void trimEditorStack(std::vector<EditorSnapshot> &stack)
{
    if (static_cast<int>(stack.size()) > kEditorUndoDepth)
        stack.erase(stack.begin());
}

void pushEditorSnapshot(EditorSnapshot snap)
{
    editorHistory.undo.push_back(std::move(snap));
    trimEditorStack(editorHistory.undo);
    editorHistory.redo.clear();
    editorHistory.pushedThisFrame = true;
}

void pushEditorHistory(const Scene &scene, const ViewState &view)
{
    if (view.playing)
        return;
    pushEditorSnapshot(captureEditorSnapshot(scene, view));
}

void applyEditorSnapshot(Scene &scene, ViewState &view, EditorSnapshot snap)
{
    scene = std::move(snap.scene);
    view.lookFrom = snap.view.lookFrom;
    view.lookAt = snap.view.lookAt;
    view.fov = snap.view.fov;
    view.aperture = snap.view.aperture;
    view.focusDistance = snap.view.focusDistance;
    view.width = snap.view.width;
    view.height = snap.view.height;
    view.samples = snap.view.samples;
    view.depth = snap.view.depth;
    view.selectedObject = snap.view.selectedObject;
    view.alsoSelected = snap.view.alsoSelected;
    view.selectedLight = snap.view.selectedLight;
    view.selectedCamera = snap.view.selectedCamera;
    view.editorFrom = snap.view.editorFrom;
    view.editorAt = snap.view.editorAt;
    view.editorFov = snap.view.editorFov;
    view.showColliders = snap.view.showColliders;
}

bool undoEditor(Scene &scene, ViewState &view)
{
    if (view.playing || editorHistory.undo.empty())
        return false;
    editorHistory.redo.push_back(captureEditorSnapshot(scene, view));
    trimEditorStack(editorHistory.redo);
    applyEditorSnapshot(scene, view, std::move(editorHistory.undo.back()));
    editorHistory.undo.pop_back();
    return true;
}

bool redoEditor(Scene &scene, ViewState &view)
{
    if (view.playing || editorHistory.redo.empty())
        return false;
    editorHistory.undo.push_back(captureEditorSnapshot(scene, view));
    trimEditorStack(editorHistory.undo);
    applyEditorSnapshot(scene, view, std::move(editorHistory.redo.back()));
    editorHistory.redo.pop_back();
    return true;
}


}
